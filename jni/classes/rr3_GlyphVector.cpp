#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

/* Not -I'd: the GL types and enums live with the Khronos thunks. */
#include "thunks/khronos/glad.h"

#include "rr3_Font.h"
#include "rr3_GlyphVector.h"
#include "trace.h"

/* GlyphVector.java's own constants, kept as they are named there. */
enum {
    BREAK_WORD_WRAP          = 0,
    BREAK_CHAR_WRAP          = 1,
    BREAK_CLIP               = 2,
    BREAK_TRUNCATE_HEAD      = 3,
    BREAK_TRUNCATE_TAIL      = 4,
    BREAK_TRUNCATE_MIDDLE    = 5,
    BREAK_WORD_WRAP_TRUNCATE = 6,
};

enum {
    PARAGRAPH_ALIGN_LEFT      = 0,
    PARAGRAPH_ALIGN_RIGHT     = 1,
    PARAGRAPH_ALIGN_CENTERED  = 2,
    PARAGRAPH_ALIGN_JUSTIFIED = 3,
    PARAGRAPH_ALIGN_NATURAL   = 4,
};

/* android.text.Layout.Alignment, which is what the switch above maps onto. */
enum { ALIGN_NORMAL = 0, ALIGN_OPPOSITE = 1, ALIGN_CENTER = 2 };

static const char *const ELLIPSIS = "\xe2\x80\xa6"; /* U+2026 */

/* ------------------------------------------------------------------------ */
/* GL                                                                        */
/* ------------------------------------------------------------------------ */

/*
 * Raw driver entry points, deliberately not find_gles1_function().
 *
 * symtable_gles1 holds softfp bridges built for the armeabi game
 * (thunks/thunk_gen_dyn.h, select_either_ptr) while this is hardfp host code;
 * android/cursor_draw.cpp:21 records what calling one of those cost the first
 * time it was tried - three float arguments dropped on the floor. Worse,
 * gl_diag_enabled() turns *every* entry into a bridge, so even the
 * integer-only ones stop being safe the moment REALRACING3_GL_DIAG is set.
 * SDL_GL_GetProcAddress hands back the driver's own pointer, the same path
 * src/rr3_control.cpp:150 and the software-decode upload at
 * src/symtab_glprobe.cpp:812 already take.
 */
struct GlyphGl {
    void   (*active_texture)(GLenum);
    void   (*gen_textures)(GLsizei, GLuint *);
    void   (*bind_texture)(GLenum, GLuint);
    void   (*tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                           GLenum, GLenum, const void *);
    void   (*tex_sub_image_2d)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei,
                               GLenum, GLenum, const void *);
    void   (*tex_parameteri)(GLenum, GLenum, GLint);
    void   (*pixel_storei)(GLenum, GLint);
    void   (*get_integerv)(GLenum, GLint *);
    GLenum (*get_error)(void);
    bool    ready;
};

template <typename T>
static T gl_entry(const char *name)
{
    return (T)SDL_GL_GetProcAddress(name);
}

static const GlyphGl &glyph_gl()
{
    static GlyphGl gl = [] {
        GlyphGl g;
        g.active_texture   = gl_entry<decltype(g.active_texture)>("glActiveTexture");
        g.gen_textures     = gl_entry<decltype(g.gen_textures)>("glGenTextures");
        g.bind_texture     = gl_entry<decltype(g.bind_texture)>("glBindTexture");
        g.tex_image_2d     = gl_entry<decltype(g.tex_image_2d)>("glTexImage2D");
        g.tex_sub_image_2d = gl_entry<decltype(g.tex_sub_image_2d)>("glTexSubImage2D");
        g.tex_parameteri   = gl_entry<decltype(g.tex_parameteri)>("glTexParameteri");
        g.pixel_storei     = gl_entry<decltype(g.pixel_storei)>("glPixelStorei");
        g.get_integerv     = gl_entry<decltype(g.get_integerv)>("glGetIntegerv");
        g.get_error        = gl_entry<decltype(g.get_error)>("glGetError");
        g.ready = g.active_texture && g.gen_textures && g.bind_texture &&
                  g.tex_image_2d && g.tex_sub_image_2d && g.tex_parameteri &&
                  g.pixel_storei && g.get_integerv && g.get_error;
        /* Say so once. A silent no-op is the failure mode the probe's
         * `if (draw) draw(...)` has at src/symtab_glprobe.cpp:221, and it is
         * indistinguishable from "the game drew nothing". */
        trace("GlyphVector GL: %s", g.ready ? "ready" : "entry points unresolved");
        return g;
    }();
    return gl;
}

/*
 * Saves the pieces of texture state the upload disturbs.
 *
 * The engine is fixed-function and keeps its state between draws, and these
 * calls land in the middle of fmFontRenderContext::drawGlyphVector. It calls
 * gR->resetTextureBindings() afterwards precisely because the Java side used
 * to bind out from under it, so restoring is belt and braces - but the unpack
 * alignment is not something it resets, and the active texture unit decides
 * which binding glBindTexture even touches.
 */
struct TextureStateGuard {
    const GlyphGl &gl;
    GLint unit = GL_TEXTURE0;
    GLint binding = 0;
    GLint alignment = 4;

    explicit TextureStateGuard(const GlyphGl &g) : gl(g)
    {
        /* Drain first, so the error read after the upload is ours and not one
         * the engine left pending - same contract as android/fb_probe.cpp:145. */
        while (gl.get_error() != GL_NO_ERROR)
            ;
        gl.get_integerv(GL_ACTIVE_TEXTURE, &unit);
        if (unit != GL_TEXTURE0)
            gl.active_texture(GL_TEXTURE0);
        /* The binding is per unit, so it can only be read once the unit is
         * pinned - otherwise the value restored below belongs to a different
         * unit than the one that was disturbed. */
        gl.get_integerv(GL_TEXTURE_BINDING_2D, &binding);
        gl.get_integerv(GL_UNPACK_ALIGNMENT, &alignment);
        if (alignment != 1)
            gl.pixel_storei(GL_UNPACK_ALIGNMENT, 1);
    }

    ~TextureStateGuard()
    {
        if (alignment != 1)
            gl.pixel_storei(GL_UNPACK_ALIGNMENT, alignment);
        gl.bind_texture(GL_TEXTURE_2D, (GLuint)binding);
        if (unit != GL_TEXTURE0)
            gl.active_texture((GLenum)unit);
        while (gl.get_error() != GL_NO_ERROR)
            ;
    }
};

/* ------------------------------------------------------------------------ */
/* UTF-8 helpers                                                             */
/* ------------------------------------------------------------------------ */

static size_t next_char_offset(const char *text, size_t bytes, size_t pos)
{
    if (pos >= bytes)
        return bytes;
    pos++;
    while (pos < bytes && ((unsigned char)text[pos] & 0xc0) == 0x80)
        pos++;
    return pos;
}

static size_t prev_char_offset(const char *text, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && ((unsigned char)text[pos] & 0xc0) == 0x80)
        pos--;
    return pos;
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                    */
/* ------------------------------------------------------------------------ */

void RR3GlyphVector::reset()
{
    free(m_text);
    free(m_lines);
    m_text = nullptr;
    m_text_bytes = 0;
    m_lines = nullptr;
    m_line_count = 0;
    m_left = m_top = m_right = m_bottom = 0;
    m_is_paragraph = false;
    m_valid = false;

    /*
     * texId is NOT cleared here. init() is only ever called on a freshly
     * constructed object (fmGlyphVectorAndroidLine's constructor is the sole
     * caller), and blanking a live id would strand a texture the engine still
     * believes it owns and will try to delete through disposeTexture.
     */
    texWidth = texHeight = 0;
    offsetX = offsetY = boundsW = boundsH = 0.0f;
    numLines = 0;
}

void RR3GlyphVector::set_paint(RR3Font *font, const char *text)
{
    if (font) {
        m_face = font->face();
        m_size = font->size();
        m_width_scale = font->width_scale();
    }
    m_valid = m_face != nullptr && text != nullptr;
    (void)text;
}

/* Joins the laid-out lines into one owned buffer and records their ranges. */
static void commit_lines(const std::vector<std::string> &lines, FontFace *face,
                         float size, float width_scale, char **out_text,
                         size_t *out_bytes, RR3GlyphVector::Line **out_lines,
                         int *out_count)
{
    size_t total = 0;
    for (const std::string &line : lines)
        total += line.size();

    char *buffer = (char *)malloc(total + 1);
    RR3GlyphVector::Line *ranges = (RR3GlyphVector::Line *)calloc(
        lines.size() ? lines.size() : 1, sizeof(RR3GlyphVector::Line));
    if (!buffer || !ranges) {
        free(buffer);
        free(ranges);
        *out_text = nullptr;
        *out_bytes = 0;
        *out_lines = nullptr;
        *out_count = 0;
        return;
    }

    size_t cursor = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        memcpy(buffer + cursor, lines[i].data(), lines[i].size());
        ranges[i].begin = (uint32_t)cursor;
        ranges[i].end = (uint32_t)(cursor + lines[i].size());
        ranges[i].width = font_face_advance(face, buffer + cursor,
                                            lines[i].size(), size, width_scale);
        cursor += lines[i].size();
    }
    buffer[total] = '\0';

    *out_text = buffer;
    *out_bytes = total;
    *out_lines = ranges;
    *out_count = (int)lines.size();
}

void RR3GlyphVector::init(RR3Font *font, const char *text)
{
    reset();
    set_paint(font, text);

    std::vector<std::string> lines;
    lines.emplace_back(text ? text : "");
    commit_lines(lines, m_face, m_size, m_width_scale, &m_text, &m_text_bytes,
                 &m_lines, &m_line_count);

    TextInk ink;
    font_face_measure(m_face, m_text, m_text_bytes, m_size, m_width_scale, &ink);
    if (ink.has_ink) {
        m_left = ink.x0;
        m_top = ink.y0;
        m_right = ink.x1;
        m_bottom = ink.y1;
    }

    /*
     * The one-pixel bleed at GlyphVector.java:142-143 is part of the contract,
     * not a rounding fudge: the engine maps the whole power-of-two texture onto
     * the quad with UVs of exactly 0 and 1 (16384 in the 2.14 fixed point the
     * vertex format uses), so there is no half-texel inset to hide a row of
     * antialiasing that lands on the boundary.
     */
    m_top -= 1;
    m_bottom += 1;

    offsetX = (jfloat)(-m_left);
    offsetY = (jfloat)(-m_top);
    boundsW = (jfloat)(m_right - m_left);
    boundsH = (jfloat)(m_bottom - m_top);

    /*
     * numLines is written even though fmGlyphVectorAndroidLine never reads it
     * back - its getLineCount() returns a constant 1 - because the field is
     * public and costs nothing to keep truthful.
     */
    numLines = 1;
    m_is_paragraph = false;
}

/* Longest prefix of [begin,end) that fits in `width`, at least one codepoint. */
static size_t fit_prefix(FontFace *face, const char *text, size_t begin,
                         size_t end, float size, float width_scale, float width)
{
    size_t fitted = begin;
    size_t cursor = begin;
    while (cursor < end) {
        size_t next = next_char_offset(text, end, cursor);
        if (font_face_advance(face, text + begin, next - begin, size,
                              width_scale) > width && fitted > begin)
            break;
        fitted = next;
        cursor = next;
    }
    if (fitted <= begin)
        fitted = next_char_offset(text, end, begin);
    return fitted;
}

/* Greedy word wrap of one hard-break-delimited run, appending to `out`. */
static void wrap_run(FontFace *face, const char *text, size_t begin, size_t end,
                     float size, float width_scale, float width,
                     std::vector<std::string> *out)
{
    if (width <= 0.0f) {
        out->emplace_back(text + begin, end - begin);
        return;
    }

    size_t line_start = begin;
    while (line_start < end) {
        size_t break_at = line_start;
        bool   found = false;
        size_t scan = line_start;

        while (scan < end) {
            size_t word_end = scan;
            while (word_end < end && text[word_end] != ' ')
                word_end++;
            if (word_end == scan) /* a space sits at `scan`; take it as a word */
                word_end = next_char_offset(text, end, scan);

            if (font_face_advance(face, text + line_start,
                                  word_end - line_start, size,
                                  width_scale) > width) {
                if (!found) {
                    /* A single word wider than the column. StaticLayout falls
                     * back to breaking inside it rather than overflowing, and
                     * so does this. */
                    break_at = fit_prefix(face, text, line_start, word_end, size,
                                          width_scale, width);
                    found = true;
                }
                break;
            }

            break_at = word_end;
            found = true;
            while (word_end < end && text[word_end] == ' ')
                word_end++;
            scan = word_end;
        }

        if (!found || break_at <= line_start)
            break_at = end;

        out->emplace_back(text + line_start, break_at - line_start);

        /* Whitespace at a wrap point belongs to the line that ended, not to
         * the one starting - Android trims it the same way. */
        size_t next = break_at;
        while (next < end && text[next] == ' ')
            next++;
        if (next <= line_start)
            break;
        line_start = next;
    }

    if (out->empty())
        out->emplace_back();
}

/* Single-line ellipsis for the three TruncateAt modes. */
static std::string truncate_line(FontFace *face, const std::string &text,
                                 float size, float width_scale, float width,
                                 int mode)
{
    const char *raw = text.c_str();
    size_t bytes = text.size();
    if (font_face_advance(face, raw, bytes, size, width_scale) <= width)
        return text;

    float ellipsis_width =
        font_face_advance(face, ELLIPSIS, strlen(ELLIPSIS), size, width_scale);
    float available = width - ellipsis_width;
    if (available < 0.0f)
        available = 0.0f;

    if (mode == BREAK_TRUNCATE_HEAD) {
        size_t start = bytes;
        while (start > 0) {
            size_t candidate = prev_char_offset(raw, start);
            if (font_face_advance(face, raw + candidate, bytes - candidate, size,
                                  width_scale) > available)
                break;
            start = candidate;
        }
        return std::string(ELLIPSIS) + text.substr(start);
    }

    if (mode == BREAK_TRUNCATE_MIDDLE) {
        size_t head = fit_prefix(face, raw, 0, bytes, size, width_scale,
                                 available * 0.5f);
        size_t tail = bytes;
        while (tail > head) {
            size_t candidate = prev_char_offset(raw, tail);
            if (candidate < head)
                break;
            if (font_face_advance(face, raw + candidate, bytes - candidate, size,
                                  width_scale) > available * 0.5f)
                break;
            tail = candidate;
        }
        return text.substr(0, head) + ELLIPSIS + text.substr(tail);
    }

    size_t head = fit_prefix(face, raw, 0, bytes, size, width_scale, available);
    return text.substr(0, head) + ELLIPSIS;
}

void RR3GlyphVector::layout_paragraph(float width, float height,
                                      int break_style, int alignment)
{
    switch (alignment) {
    case PARAGRAPH_ALIGN_RIGHT:    m_alignment = ALIGN_OPPOSITE; break;
    case PARAGRAPH_ALIGN_CENTERED: m_alignment = ALIGN_CENTER;   break;
    /* Justified is listed by the engine but android.text.Layout has no such
     * alignment, so GlyphVector.java's switch falls through to NORMAL. */
    default:                       m_alignment = ALIGN_NORMAL;   break;
    }

    if (break_style == BREAK_CHAR_WRAP)
        break_style = BREAK_WORD_WRAP; /* Android logs "unsupported" and does this */

    const char *source = m_text ? m_text : "";
    size_t source_bytes = m_text_bytes;

    std::vector<std::string> lines;
    bool truncating = break_style == BREAK_TRUNCATE_HEAD ||
                      break_style == BREAK_TRUNCATE_TAIL ||
                      break_style == BREAK_TRUNCATE_MIDDLE;

    if (truncating) {
        lines.push_back(truncate_line(m_face, std::string(source, source_bytes),
                                      m_size, m_width_scale, width,
                                      break_style));
    } else {
        size_t run_start = 0;
        for (size_t i = 0; i <= source_bytes; i++) {
            if (i == source_bytes || source[i] == '\n') {
                wrap_run(m_face, source, run_start, i, m_size, m_width_scale,
                         width, &lines);
                run_start = i + 1;
            }
        }
    }
    if (lines.empty())
        lines.emplace_back();

    /*
     * Clip and WordWrapTruncate are the two styles that also honour the height
     * budget (GlyphVector.java:219 and :232). Clip simply drops the lines that
     * do not fit; WordWrapTruncate ellipsises the last one that does.
     */
    if ((break_style == BREAK_CLIP || break_style == BREAK_WORD_WRAP_TRUNCATE) &&
        height > 0.0f && m_line_height > 0) {
        size_t keep = lines.size();
        for (size_t i = 0; i < lines.size(); i++) {
            if ((float)((int)(i + 1) * m_line_height) > height) {
                keep = i;
                break;
            }
        }
        if (keep < 1)
            keep = 1;
        if (keep < lines.size()) {
            lines.resize(keep);
            if (break_style == BREAK_WORD_WRAP_TRUNCATE) {
                std::string &last = lines.back();
                last = truncate_line(m_face, last + " " + ELLIPSIS, m_size,
                                     m_width_scale, width, BREAK_TRUNCATE_TAIL);
            }
        }
    }

    char *joined = nullptr;
    size_t joined_bytes = 0;
    Line *ranges = nullptr;
    int count = 0;
    commit_lines(lines, m_face, m_size, m_width_scale, &joined, &joined_bytes,
                 &ranges, &count);

    free(m_text);
    free(m_lines);
    m_text = joined;
    m_text_bytes = joined_bytes;
    m_lines = ranges;
    m_line_count = count;
}

float RR3GlyphVector::line_origin_x(const Line &line) const
{
    switch (m_alignment) {
    case ALIGN_OPPOSITE: return m_layout_width - line.width;
    case ALIGN_CENTER:   return (m_layout_width - line.width) * 0.5f;
    default:             return 0.0f;
    }
}

void RR3GlyphVector::init_with_paragraph(RR3Font *font, const char *text,
                                         float width, float height,
                                         int break_style, int alignment)
{
    reset();
    set_paint(font, text);

    if (width < 0.0f)
        width = 0.0f;
    if (height < 0.0f)
        height = 0.0f;
    m_layout_width = width;
    m_is_paragraph = true;

    font_face_line_box(m_face, m_size, &m_baseline, &m_line_height);

    /* Seed m_text with the source so layout_paragraph can slice it, then let
     * it replace the buffer with the laid-out lines. */
    size_t bytes = text ? strlen(text) : 0;
    m_text = (char *)malloc(bytes + 1);
    if (m_text) {
        memcpy(m_text, text ? text : "", bytes);
        m_text[bytes] = '\0';
        m_text_bytes = bytes;
    }

    layout_paragraph(width, height, break_style, alignment);

    /*
     * findLayoutBounds (GlyphVector.java:100): the union of every line's
     * left/right - which come from the alignment, not from the ink - and of
     * every line's top/bottom, which are the integer line box.
     */
    for (int i = 0; i < m_line_count; i++) {
        float left = line_origin_x(m_lines[i]);
        int line_left = (int)floorf(left);
        int line_right = (int)ceilf(left + m_lines[i].width);
        int line_top = i * m_line_height;
        int line_bottom = (i + 1) * m_line_height;

        if (i == 0 || line_left < m_left)     m_left = line_left;
        if (i == 0 || line_top < m_top)       m_top = line_top;
        if (i == 0 || line_right > m_right)   m_right = line_right;
        if (i == 0 || line_bottom > m_bottom) m_bottom = line_bottom;
    }

    /*
     * The first line's ink can reach above its layout box - accents and tall
     * capitals in a face whose ascent metric is tighter than its outlines -
     * so the top is pulled up to cover it (GlyphVector.java:267).
     */
    if (m_line_count > 0) {
        TextInk ink;
        font_face_measure(m_face, m_text + m_lines[0].begin,
                          m_lines[0].end - m_lines[0].begin, m_size,
                          m_width_scale, &ink);
        int ink_top = m_baseline + (ink.has_ink ? ink.y0 : 0);
        if (ink_top < m_top)
            m_top = ink_top;
    }

    m_top -= 1;
    m_bottom += 1;

    numLines = m_line_count;
    if (numLines > 0) {
        /* offsetX is pinned to zero here and only here. The Paragraph variant
         * of getDrawPositionOffset returns {-offsetX, -offsetY} while the Line
         * variant discards offsetX entirely, so a non-zero value would shift
         * paragraphs and nothing else. */
        offsetX = 0.0f;
        offsetY = (jfloat)(m_baseline - m_top);
    }
    boundsW = (jfloat)(m_right - m_left);
    boundsH = (jfloat)(m_bottom - m_top);
}

/* ------------------------------------------------------------------------ */
/* Rasterisation and upload                                                  */
/* ------------------------------------------------------------------------ */

unsigned char *RR3GlyphVector::rasterise(int width, int height, int stride,
                                         float pen_x, float pen_y,
                                         float size_scale) const
{
    unsigned char *coverage =
        (unsigned char *)calloc((size_t)stride * (size_t)height, 1);
    if (!coverage)
        return nullptr;

    float size = m_size * size_scale;

    if (!m_is_paragraph) {
        font_face_draw(m_face, m_text, m_text_bytes, size, m_width_scale,
                       coverage, width, height, stride, pen_x, pen_y);
        return coverage;
    }

    for (int i = 0; i < m_line_count; i++) {
        const Line &line = m_lines[i];
        font_face_draw(m_face, m_text + line.begin, line.end - line.begin, size,
                       m_width_scale, coverage, width, height, stride,
                       pen_x + line_origin_x(line) * size_scale,
                       pen_y + (float)(i * m_line_height) * size_scale);
    }
    return coverage;
}

jboolean RR3GlyphVector::create_texture()
{
    if (!m_valid || !m_text)
        return JNI_FALSE;

    const GlyphGl &gl = glyph_gl();
    if (!gl.ready)
        return JNI_FALSE;

    /* Next power of two at or above the bounds, exactly as the Java loops do
     * it - starting at 1 and doubling *while* smaller, so a 1-pixel-wide box
     * still gets a 1-wide texture. The engine derives the quad's UVs from
     * texWidth/texHeight, so getting this wrong scales the text rather than
     * merely wasting memory. */
    int width = 1;
    while (width < m_right - m_left)
        width *= 2;
    int height = 1;
    while (height < m_bottom - m_top)
        height *= 2;

    unsigned char *coverage = rasterise(width, height, width,
                                        (float)(-m_left),
                                        (float)(m_is_paragraph
                                                    ? m_baseline - m_top
                                                    : -m_top),
                                        1.0f);
    if (!coverage)
        return JNI_FALSE;

    /*
     * RGBA4444, premultiplied, matching the ARGB_4444 bitmap Android handed to
     * GLUtils.texImage2D. Both halves of that matter:
     *
     *  - drawGlyphVector binds the ui_image material (CommonMaterials MatId 1),
     *    the same shader every UI sprite uses, so the sampler reads RGBA - not
     *    GL_ALPHA.
     *  - it also calls enableBlend(1, 5) unconditionally, which the engine's
     *    blend table resolves to GL_ONE / GL_ONE_MINUS_SRC_ALPHA. That is
     *    premultiplied alpha, and for white antialiased text premultiplied
     *    means every channel equals the coverage.
     *
     * Keeping 4 bits per channel rather than widening to RGBA8888 also keeps
     * the texture the size GlyphVectorLRU thinks it is: fmGlyphVector::getSize
     * reports boundsW * boundsH * 2, i.e. two bytes per pixel.
     */
    std::vector<GLushort> pixels((size_t)width * (size_t)height);
    for (size_t i = 0; i < pixels.size(); i++) {
        unsigned int nibble = coverage[i] >> 4;
        pixels[i] = (GLushort)(nibble * 0x1111u);
    }
    free(coverage);

    TextureStateGuard guard(gl);

    GLuint id = 0;
    gl.gen_textures(1, &id);
    if (id == 0) {
        trace("GlyphVector: glGenTextures returned 0 - no GL context on this thread?");
        return JNI_FALSE;
    }

    gl.bind_texture(GL_TEXTURE_2D, id);
    /* glTexParameteri, not the Java's glTexParameterf(..., 9729.0f): identical
     * effect with no float crossing an ABI boundary. These are per texture
     * object, so nothing here needs restoring. */
    gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                    GL_UNSIGNED_SHORT_4_4_4_4, pixels.data());

    GLenum error = gl.get_error();
    if (error != GL_NO_ERROR) {
        trace("GlyphVector: %dx%d upload failed (0x%04x)", width, height, error);
        /* The texture name is left for the engine to reclaim: disposeTexture
         * is the only thing allowed to call glDeleteTextures on it, and it
         * only runs for a texId this side published. Publishing nothing means
         * leaking one name on a path that should not happen. */
        return JNI_FALSE;
    }

    texWidth = width;
    texHeight = height;
    texId = (jint)id;
    return JNI_TRUE;
}

jboolean RR3GlyphVector::render_to_texture(jint gl_texture, jint width,
                                           jint height, float scale)
{
    /*
     * CarLiveryBaker::bakeSymbol is the only caller - the number painted onto
     * a car's livery - and only the Line variant of fmGlyphVectorAndroid
     * implements it; the Paragraph one returns false outright.
     */
    if (m_is_paragraph || !m_valid || !m_text || width <= 0 || height <= 0)
        return JNI_FALSE;

    const GlyphGl &gl = glyph_gl();
    if (!gl.ready)
        return JNI_FALSE;

    /*
     * The canvas transform at GlyphVector.java:285-288 is translate(0,h) then
     * scale(1,-1) then a centring translate then scale(f,f). Scaling the
     * canvas by f is the same as rasterising at f times the em size, so the
     * only part that needs doing by hand is the vertical flip, applied to the
     * finished rows.
     */
    float pen_x = ((float)width - (float)(m_right - m_left) * scale) * 0.5f -
                  (float)m_left * scale;
    float pen_y = ((float)height - (float)(m_bottom - m_top) * scale) * 0.5f -
                  (float)m_top * scale;

    unsigned char *coverage =
        rasterise(width, height, width, pen_x, pen_y, scale);
    if (!coverage)
        return JNI_FALSE;

    for (int row = 0; row < height / 2; row++) {
        unsigned char *a = coverage + (size_t)row * (size_t)width;
        unsigned char *b = coverage + (size_t)(height - 1 - row) * (size_t)width;
        for (int col = 0; col < width; col++) {
            unsigned char tmp = a[col];
            a[col] = b[col];
            b[col] = tmp;
        }
    }

    TextureStateGuard guard(gl);

    /* The destination belongs to the engine: it hands over the id and the
     * dimensions, and GL_ALPHA/GL_UNSIGNED_BYTE is what the Java passed to
     * GLUtils.texSubImage2D for an ALPHA_8 bitmap. If the engine created that
     * texture with an incompatible internal format this is where it shows up,
     * which is why the error is read rather than assumed away. */
    gl.bind_texture(GL_TEXTURE_2D, (GLuint)gl_texture);
    gl.tex_sub_image_2d(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_ALPHA,
                        GL_UNSIGNED_BYTE, coverage);
    GLenum error = gl.get_error();
    free(coverage);

    if (error != GL_NO_ERROR) {
        trace("GlyphVector: renderToTexture %dx%d failed (0x%04x)", width,
              height, error);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

/* ------------------------------------------------------------------------ */
/* JNI registration                                                          */
/* ------------------------------------------------------------------------ */

/*
 * Trampolines rather than member functions: ManagedMethod::Register takes
 * `auto *F`, a plain function pointer, and asserts the second parameter is a
 * jobject (jni/jni_internals.h:230). The engine's `this` arrives there and is
 * cast by hand - this JNI has no notion of a receiver.
 */

/*
 * The jclass is what makes this a four-argument dispatcher, and NewObject
 * needs exactly that: iface_CallNonVirtualMethodV (jni/jni.cpp:906) casts
 * addr_variadic to (JNIEnv*, jobject, jclass, va_list) without consulting
 * takes_class. The placement new is what installs the vptr - NewObject callocs
 * the storage (jni/jni.cpp:215) and a zero vptr turns the first
 * GetObjectClass into a jump to address 0.
 */
static void gv_ctor(JNIEnv *, jobject self, jclass) { new (self) RR3GlyphVector(); }

static void gv_init(JNIEnv *, jobject self, jobject font, jstring text)
{
    /* The string was built by JNIEnv::NewString from UTF-16 (jni/jni.cpp:481),
     * which stores it converted to UTF-8, so the buffer can be read straight
     * off the object the way android_assets.cpp:143 does. */
    const char *utf8 = text ? ((String *)text)->str : nullptr;
    ((RR3GlyphVector *)self)->init((RR3Font *)font, utf8);
}

static void gv_init_with_paragraph(JNIEnv *, jobject self, jobject font,
                                   jstring text, jfloat width, jfloat height,
                                   jint break_style, jint alignment)
{
    const char *utf8 = text ? ((String *)text)->str : nullptr;
    ((RR3GlyphVector *)self)->init_with_paragraph((RR3Font *)font, utf8, width,
                                                  height, break_style,
                                                  alignment);
}

static jboolean gv_create_texture(JNIEnv *, jobject self)
{
    return ((RR3GlyphVector *)self)->create_texture();
}

static jboolean gv_render_to_texture(JNIEnv *, jobject self, jint texture,
                                     jint width, jint height, jfloat scale)
{
    return ((RR3GlyphVector *)self)->render_to_texture(texture, width, height,
                                                       scale);
}

static const FieldId RR3GlyphVectorFields[] = {
    REGISTER_FIELD(RR3GlyphVector, texId),
    REGISTER_FIELD(RR3GlyphVector, texWidth),
    REGISTER_FIELD(RR3GlyphVector, texHeight),
    REGISTER_FIELD(RR3GlyphVector, offsetX),
    REGISTER_FIELD(RR3GlyphVector, offsetY),
    REGISTER_FIELD(RR3GlyphVector, boundsW),
    REGISTER_FIELD(RR3GlyphVector, boundsH),
    REGISTER_FIELD(RR3GlyphVector, numLines),
    {NULL},
};

/*
 * Signatures are byte-for-byte what emulator.log:10591 records the engine
 * asking for. GetMethodID strcmps the name AND the signature
 * (jni/jni.cpp:298), so a plausible-looking descriptor never matches and the
 * engine gets exactly the NULL it got while the class did not exist at all.
 */
static const ManagedMethod RR3GlyphVectorMethods[] = {
    ManagedMethod::RegisterNonVirtual<&gv_ctor>(
        RR3GlyphVector::clazz, "<init>", "()V"),
    ManagedMethod::Register<&gv_init>(
        RR3GlyphVector::clazz, "init",
        "(Lcom/firemint/realracing3/Font;Ljava/lang/String;)V"),
    ManagedMethod::Register<&gv_init_with_paragraph>(
        RR3GlyphVector::clazz, "initWithParagraph",
        "(Lcom/firemint/realracing3/Font;Ljava/lang/String;FFII)V"),
    ManagedMethod::Register<&gv_create_texture>(
        RR3GlyphVector::clazz, "createTexture", "()Z"),
    ManagedMethod::Register<&gv_render_to_texture>(
        RR3GlyphVector::clazz, "renderToTexture", "(IIIF)Z"),
    {NULL},
};

Class RR3GlyphVector::clazz = {
    .classpath = "com/firemint/realracing3/GlyphVector",
    .classname = "GlyphVector",
    .managed_methods = RR3GlyphVectorMethods,
    .native_methods = {NULL},
    .fields = RR3GlyphVectorFields,
    .instance_size = sizeof(RR3GlyphVector),
};

static const int registered = ClassRegistry::register_class(RR3GlyphVector::clazz);

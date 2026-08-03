#include "rr3_font_face.h"

#include <limits.h>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "third_party/stb/stb_truetype.h"

#include "fix_path.h"
#include "trace.h"

struct FontFace {
    char           *engine_path;   /* the cache key, as the engine spelled it */
    unsigned char  *bytes;         /* whole file; stb parses it in place */
    stbtt_fontinfo  info;
    float           em_scale;      /* pixels per font unit at a 1px em size */
    int             ascent, descent, line_gap;  /* font units */
    int             bbox_y0, bbox_y1;           /* font units */
};

static std::mutex               g_faces_lock;
static std::vector<FontFace *>  g_faces;

/* Also caches the failures: the engine retries the same missing file every
 * time it builds a font, and one complaint per path is the useful amount. */
static std::vector<char *>      g_failed;

static unsigned char *read_whole_file(const char *path, size_t *out_size)
{
    char translated[PATH_MAX];
    FILE *f = fopen(fix_path(path, translated, sizeof(translated)), "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    unsigned char *bytes = (unsigned char *)malloc((size_t)size);
    if (!bytes) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(bytes, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(bytes);
        return NULL;
    }
    *out_size = got;
    return bytes;
}

FontFace *font_face_open(const char *engine_path)
{
    if (!engine_path || !*engine_path)
        return NULL;

    std::lock_guard<std::mutex> guard(g_faces_lock);

    for (FontFace *face : g_faces)
        if (strcmp(face->engine_path, engine_path) == 0)
            return face;
    for (char *failed : g_failed)
        if (strcmp(failed, engine_path) == 0)
            return NULL;

    size_t size = 0;
    unsigned char *bytes = read_whole_file(engine_path, &size);
    FontFace *face = bytes ? (FontFace *)calloc(1, sizeof(FontFace)) : NULL;

    /*
     * stbtt_InitFont wants the offset of the face inside the file, which for a
     * bare .otf/.ttf is 0 but for a collection is not. Asking for face 0 by
     * index covers both without special-casing the container.
     */
    int offset = face ? stbtt_GetFontOffsetForIndex(bytes, 0) : -1;
    if (offset < 0 || (face && !stbtt_InitFont(&face->info, bytes, offset))) {
        free(face);
        free(bytes);
        face = NULL;
    }

    if (!face) {
        /* Not fatal. A missing typeface costs the strings drawn with it, and
         * the caller falls back to synthetic metrics so the surrounding layout
         * keeps its shape instead of collapsing to zero-sized boxes. */
        trace("font: cannot load '%s' - text using it will not be drawn",
              engine_path);
        g_failed.push_back(strdup(engine_path));
        return NULL;
    }

    face->engine_path = strdup(engine_path);
    face->bytes = bytes;
    face->em_scale = stbtt_ScaleForMappingEmToPixels(&face->info, 1.0f);
    stbtt_GetFontVMetrics(&face->info, &face->ascent, &face->descent,
                          &face->line_gap);

    int x0 = 0, x1 = 0;
    stbtt_GetFontBoundingBox(&face->info, &x0, &face->bbox_y0, &x1,
                             &face->bbox_y1);

    g_faces.push_back(face);
    trace("font: loaded '%s' (%zu bytes, %d glyphs, cff=%d)", engine_path, size,
          face->info.numGlyphs, face->info.cff.size ? 1 : 0);
    return face;
}

void font_face_vmetrics(FontFace *face, float px_size, FontVMetrics *out)
{
    memset(out, 0, sizeof(*out));
    if (!face)
        return;

    float scale = face->em_scale * px_size;

    /*
     * stb reports ascent above the baseline as positive and descent below it
     * as negative; Skia's FontMetrics is the other way round for ascent. The
     * Java class then negates ascent and top again on the way out
     * (Font.java:84-88), so what native reads is "both distances, positive".
     */
    out->ascent  =  face->ascent  * scale;
    out->descent = -face->descent * scale;
    out->height  = out->ascent + out->descent;
    out->top     =  face->bbox_y1 * scale;
    out->bottom  = -face->bbox_y0 * scale;
    out->leading = face->line_gap * scale;
}

void font_face_line_box(FontFace *face, float px_size, int *baseline,
                        int *line_height)
{
    FontVMetrics vm;
    font_face_vmetrics(face, px_size, &vm);

    /*
     * Paint.getFontMetricsInt rounds away from the baseline in both
     * directions - floor on the (negative) ascent, ceil on the descent - so
     * the integer box never clips what the float one covered.
     */
    int ascent_i  = (int)ceilf(vm.ascent);
    int descent_i = (int)ceilf(vm.descent);

    *baseline = ascent_i;
    *line_height = ascent_i + descent_i;
    if (*line_height <= 0)
        *line_height = 1;
}

/*
 * One codepoint off a UTF-8 run.
 *
 * Malformed input decodes to U+FFFD and advances one byte, which is what
 * java.lang.String would have ended up holding anyway: the text reached us
 * through JNIEnv::NewString, so it was UTF-16 that jni.cpp:481 already
 * converted, and anything invalid was replaced there.
 */
static uint32_t next_codepoint(const char *s, size_t bytes, size_t *pos)
{
    unsigned char c = (unsigned char)s[*pos];
    size_t remaining = bytes - *pos;

    if (c < 0x80) {
        (*pos)++;
        return c;
    }
    if ((c & 0xe0) == 0xc0 && remaining >= 2) {
        uint32_t cp = ((uint32_t)(c & 0x1f) << 6) |
                      ((unsigned char)s[*pos + 1] & 0x3f);
        *pos += 2;
        return cp;
    }
    if ((c & 0xf0) == 0xe0 && remaining >= 3) {
        uint32_t cp = ((uint32_t)(c & 0x0f) << 12) |
                      ((uint32_t)((unsigned char)s[*pos + 1] & 0x3f) << 6) |
                      ((unsigned char)s[*pos + 2] & 0x3f);
        *pos += 3;
        return cp;
    }
    if ((c & 0xf8) == 0xf0 && remaining >= 4) {
        uint32_t cp = ((uint32_t)(c & 0x07) << 18) |
                      ((uint32_t)((unsigned char)s[*pos + 1] & 0x3f) << 12) |
                      ((uint32_t)((unsigned char)s[*pos + 2] & 0x3f) << 6) |
                      ((unsigned char)s[*pos + 3] & 0x3f);
        *pos += 4;
        return cp;
    }

    (*pos)++;
    return 0xfffd;
}

size_t font_face_encode_utf8(uint32_t cp, char *buf)
{
    if (cp < 0x80) {
        buf[0] = (char)cp;
        buf[1] = '\0';
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (char)(0xc0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3f));
        buf[2] = '\0';
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (char)(0xe0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        buf[2] = (char)(0x80 | (cp & 0x3f));
        buf[3] = '\0';
        return 3;
    }
    buf[0] = (char)(0xf0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    buf[3] = (char)(0x80 | (cp & 0x3f));
    buf[4] = '\0';
    return 4;
}

/*
 * U+2060 WORD JOINER is an invisible break-control character. Android dropped
 * it below API 19 (GlyphVector.java:53) because the platform font had no glyph
 * for it; here every face is a single game-supplied file with no fallback, so
 * leaving it in would draw .notdef - a visible box in the middle of a string.
 * It is dropped unconditionally for that reason, not to emulate an API level.
 */
static bool is_invisible(uint32_t cp)
{
    return cp == 0x2060 || cp == 0xfeff || cp == 0x200b;
}

void font_face_measure(FontFace *face, const char *utf8, size_t bytes,
                       float px_size, float width_scale, TextInk *out)
{
    memset(out, 0, sizeof(*out));
    if (!face || !utf8 || bytes == 0)
        return;

    float scale_y = face->em_scale * px_size;
    float scale_x = scale_y * width_scale;
    float pen = 0.0f;

    size_t pos = 0;
    while (pos < bytes) {
        uint32_t cp = next_codepoint(utf8, bytes, &pos);
        if (is_invisible(cp))
            continue;

        int glyph = stbtt_FindGlyphIndex(&face->info, (int)cp);
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &lsb);

        int origin = (int)floorf(pen);
        float shift = pen - (float)origin;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&face->info, glyph, scale_x, scale_y,
                                        shift, 0.0f, &x0, &y0, &x1, &y1);
        if (x1 > x0 && y1 > y0) {
            if (!out->has_ink) {
                out->x0 = origin + x0;
                out->y0 = y0;
                out->x1 = origin + x1;
                out->y1 = y1;
                out->has_ink = 1;
            } else {
                if (origin + x0 < out->x0) out->x0 = origin + x0;
                if (y0 < out->y0)          out->y0 = y0;
                if (origin + x1 > out->x1) out->x1 = origin + x1;
                if (y1 > out->y1)          out->y1 = y1;
            }
        }

        pen += (float)advance * scale_x;
    }

    out->advance = pen;
}

float font_face_advance(FontFace *face, const char *utf8, size_t bytes,
                        float px_size, float width_scale)
{
    if (!face || !utf8 || bytes == 0)
        return 0.0f;

    float scale_x = face->em_scale * px_size * width_scale;
    float pen = 0.0f;

    size_t pos = 0;
    while (pos < bytes) {
        uint32_t cp = next_codepoint(utf8, bytes, &pos);
        if (is_invisible(cp))
            continue;

        int glyph = stbtt_FindGlyphIndex(&face->info, (int)cp);
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &lsb);
        pen += (float)advance * scale_x;
    }
    return pen;
}

void font_face_draw(FontFace *face, const char *utf8, size_t bytes,
                    float px_size, float width_scale,
                    unsigned char *dst, int dst_w, int dst_h, int dst_stride,
                    float pen_x, float baseline_y)
{
    if (!face || !utf8 || bytes == 0 || !dst || dst_w <= 0 || dst_h <= 0)
        return;

    float scale_y = face->em_scale * px_size;
    float scale_x = scale_y * width_scale;
    float pen = pen_x;

    int   baseline_i = (int)floorf(baseline_y);
    float shift_y = baseline_y - (float)baseline_i;

    std::vector<unsigned char> coverage;

    size_t pos = 0;
    while (pos < bytes) {
        uint32_t cp = next_codepoint(utf8, bytes, &pos);
        if (is_invisible(cp))
            continue;

        int glyph = stbtt_FindGlyphIndex(&face->info, (int)cp);
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &lsb);

        int origin = (int)floorf(pen);
        float shift_x = pen - (float)origin;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&face->info, glyph, scale_x, scale_y,
                                        shift_x, shift_y, &x0, &y0, &x1, &y1);

        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            /*
             * Rasterised into scratch and blitted rather than straight into
             * dst: the glyph box can hang off any edge of the texture (the
             * one-pixel padding the contract adds at top and bottom is not
             * enough for an italic overhang on the last character) and
             * stbtt_MakeGlyphBitmapSubpixel does no clipping of its own.
             */
            coverage.assign((size_t)gw * (size_t)gh, 0);
            stbtt_MakeGlyphBitmapSubpixel(&face->info, coverage.data(), gw, gh,
                                          gw, scale_x, scale_y, shift_x,
                                          shift_y, glyph);

            for (int row = 0; row < gh; row++) {
                int dy = baseline_i + y0 + row;
                if (dy < 0 || dy >= dst_h)
                    continue;
                const unsigned char *src = &coverage[(size_t)row * (size_t)gw];
                unsigned char *out_row = dst + (size_t)dy * (size_t)dst_stride;
                for (int col = 0; col < gw; col++) {
                    int dx = origin + x0 + col;
                    if (dx < 0 || dx >= dst_w)
                        continue;
                    if (src[col] > out_row[dx])
                        out_row[dx] = src[col];
                }
            }
        }

        pen += (float)advance * scale_x;
    }
}

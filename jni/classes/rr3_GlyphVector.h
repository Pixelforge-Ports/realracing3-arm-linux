#pragma once

#include <stddef.h>
#include <stdint.h>

#include "jni.h"
#include "jni_internals.h"

#include "rr3_font_face.h"

class RR3Font;

/*
 * com/firemint/realracing3/GlyphVector.
 *
 * fmGlyphVectorJNI builds one of these for every string the UI draws AND for
 * every string it merely measures: fmFontStaticMetrics::stringWidth /
 * stringHeight / stringLineCount construct one purely to read boundsW, boundsH
 * and numLines back. So while the class is missing the menu is not just
 * invisible, it is laid out at zero size - which is what the 8008
 * "Could not create new Java object instance" lines in the log were costing.
 */
class RR3GlyphVector : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    /*
     * The eight fields fmGlyphVectorJNI's constructor resolves, transcribed
     * from what the engine asked for at emulator.log:10591 rather than from
     * GlyphVector.java. Names and widths both matter and they fail differently:
     * GetFieldID matches on the name alone (jni/jni.cpp:318), so a wrong name
     * is a loud NULL while a wrong C++ width is a silent misread of these
     * bytes.
     *
     * texId starts at -1 and that is load-bearing. fmGlyphVectorJNI::init reads
     * texId straight back after calling init(), and fmFontRenderContext::
     * drawGlyphVector only calls createTexture() when the cached value is
     * negative. A zero here reads as "there is already a texture", createTexture
     * is never called, and there is still no text.
     */
    jint   texId = -1;
    jint   texWidth = 0;
    jint   texHeight = 0;
    jfloat offsetX = 0.0f;
    jfloat offsetY = 0.0f;
    jfloat boundsW = 0.0f;
    jfloat boundsH = 0.0f;
    jint   numLines = 0;

    /* One laid-out line: a byte range into m_text plus its measured width. */
    struct Line {
        uint32_t begin;
        uint32_t end;
        float    width;
    };

    void init(RR3Font *font, const char *text);
    void init_with_paragraph(RR3Font *font, const char *text, float width,
                             float height, int break_style, int alignment);
    jboolean create_texture();
    jboolean render_to_texture(jint gl_texture, jint width, jint height,
                               float scale);

private:
    void reset();
    void set_paint(RR3Font *font, const char *text);
    void layout_paragraph(float width, float height, int break_style,
                          int alignment);
    float line_origin_x(const Line &line) const;
    unsigned char *rasterise(int width, int height, int stride, float pen_x,
                             float pen_y, float size_scale) const;

    FontFace *m_face = nullptr;
    float     m_size = 0.0f;
    float     m_width_scale = 1.0f;

    /*
     * Owned copy of the string, kept for as long as the object lives because
     * createTexture() is called lazily and can be called again: fmGlyphVector
     * AndroidLine::invalidateTexture drops texId back to -1 without deleting
     * the GL object, and the next draw re-runs createTexture from scratch.
     * Only the pixels are released early, which is what Bitmap.recycle() at
     * GlyphVector.java:96 does.
     *
     * These allocations are never freed, matching the object itself: the shim's
     * DeleteGlobalRef is a no-op (jni/jni.cpp:178) and NewObjectV callocs
     * without a matching free, so a run leaks its ~8000 glyph vectors either
     * way. What must not happen is retaining a bitmap per object on top of it.
     */
    char     *m_text = nullptr;
    size_t    m_text_bytes = 0;

    Line     *m_lines = nullptr;
    int       m_line_count = 0;

    /* Paint.getTextBounds / findLayoutBounds, in pixels. */
    int m_left = 0, m_top = 0, m_right = 0, m_bottom = 0;

    int   m_alignment = 0;
    float m_layout_width = 0.0f;
    int   m_baseline = 0;
    int   m_line_height = 0;
    bool  m_is_paragraph = false;
    bool  m_valid = false;
};

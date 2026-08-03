#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

#include "rr3_Font.h"
#include "trace.h"

jboolean RR3Font::init(JNIEnv *, jobject, jstring path, jboolean bold,
                       jboolean italic, jfloat size, jfloat width_scale)
{
    /*
     * fmFontJNI::init resolves the name through Asset::GetFullPath and hands
     * over an absolute path via NewStringUTF, so this is the game's own
     * spelling of one of the thirteen typefaces the player's copy ships. The
     * port never carries a font of its own: if the tree is missing the file,
     * the text that uses it is simply not drawn.
     */
    const char *file = path ? ((String *)path)->str : nullptr;

    m_size = size;
    m_width_scale = width_scale > 0.0f ? width_scale : 1.0f;
    m_face = font_face_open(file);

    /*
     * Typeface.create(typeface, style) would synthesise a bold or italic face
     * when the file has neither. Nothing here does: the game asks for the
     * variant it wants by filename - EurostileLTStd-Bold.otf,
     * -BoldOblique.otf, minion_pro_italic.otf - and these two flags have been
     * false on every init in the captured runs.
     */
    (void)bold;
    (void)italic;

    if (!m_face) {
        /*
         * fmFontJNI::init reads the six metrics back whether or not this
         * returned true, so leaving them at zero would collapse every box laid
         * out with this font. Proportions of a typical Latin face keep the UI
         * the right shape while the strings themselves stay blank, which is a
         * legible failure instead of a scrambled one.
         */
        ascent = size * 0.8f;
        descent = size * 0.2f;
        height = ascent + descent;
        top = ascent;
        bottom = descent;
        leading = 0.0f;
        return JNI_FALSE;
    }

    FontVMetrics metrics;
    font_face_vmetrics(m_face, m_size, &metrics);
    ascent = metrics.ascent;
    descent = metrics.descent;
    height = metrics.height;
    top = metrics.top;
    bottom = metrics.bottom;
    leading = metrics.leading;
    return JNI_TRUE;
}

/*
 * The em size, not the line height. Font.java:44 returns m_size, and
 * GlyphVector feeds this straight into Paint.setTextSize - so returning
 * ascent + descent here would rasterise every string at the wrong size.
 */
jfloat RR3Font::getSize(JNIEnv *, jobject) { return m_size; }

jboolean RR3Font::loadGlyph(JNIEnv *, jobject, jint codepoint)
{
    if (!m_face)
        return JNI_FALSE;

    char utf8[5];
    size_t bytes = font_face_encode_utf8((uint32_t)codepoint, utf8);

    TextInk ink;
    font_face_measure(m_face, utf8, bytes, m_size, m_width_scale, &ink);

    glyphOffX = (jfloat)ink.x0;
    glyphOffY = (jfloat)(-ink.y0);
    glyphWidth = (jfloat)(ink.x1 - ink.x0);
    glyphHeight = (jfloat)(ink.y1 - ink.y0);
    glyphAdvance = ink.advance;
    return JNI_TRUE;
}

jboolean RR3Font::loadBitmap(JNIEnv *env, jobject, jint codepoint, jfloat off_x,
                             jfloat off_y, jfloat outline_width,
                             jint outline_style)
{
    bmpLeft = bmpTop = bmpWidth = bmpHeight = bmpPitch = 0;
    if (!m_face)
        return JNI_FALSE;

    char utf8[5];
    size_t bytes = font_face_encode_utf8((uint32_t)codepoint, utf8);

    TextInk ink;
    font_face_measure(m_face, utf8, bytes, m_size, m_width_scale, &ink);

    /*
     * Font.java:101. OUTLINE_STROKE and OUTLINE_OUTSIDE grow the box by the
     * outline width; the other two styles reserve a single pixel. The outline
     * itself is not drawn here - Paint.setStyle(STROKE) has no equivalent in
     * the rasteriser - so a stroked glyph comes out as its fill inside a box
     * of the size the engine expects. Nothing in the captured runs reaches
     * this path: ManagerBackendAndroid::loadBitmap is the dynamic font
     * backend, and RR3's menus use the GlyphVector path instead.
     */
    float pad = (outline_style == 1 || outline_style == 3)
                    ? 1.0f + outline_width
                    : 1.0f;
    float shift_y = -off_y;

    float left = floorf(((float)ink.x0 + off_x) - pad);
    float right = ceilf((float)ink.x1 + off_x + pad);
    float top_edge = floorf(((float)ink.y0 + shift_y) - pad);
    float bottom_edge = ceilf((float)ink.y1 + shift_y + pad);

    bmpLeft = (jint)left;
    bmpTop = -(jint)top_edge;
    bmpWidth = (jint)(right - left);
    bmpHeight = (jint)(bottom_edge - top_edge);
    if (bmpWidth <= 0 || bmpHeight <= 0) {
        bmpWidth = bmpHeight = 0;
        return JNI_FALSE;
    }

    size_t pixels = (size_t)bmpWidth * (size_t)bmpHeight;
    unsigned char *coverage = (unsigned char *)calloc(pixels, 1);
    if (!coverage) {
        bmpWidth = bmpHeight = 0;
        return JNI_FALSE;
    }
    font_face_draw(m_face, utf8, bytes, m_size, m_width_scale, coverage,
                   bmpWidth, bmpHeight, bmpWidth, off_x - left,
                   shift_y - top_edge);

    /*
     * Java allocated a fresh byte[] per call and let the collector take the
     * previous one. There is no collector here and DeleteLocalRef is a no-op
     * (jni/jni.cpp), so the storage behind the last array is released by hand
     * before the next is published - otherwise every glyph of every dynamic
     * font would stay resident for the life of the process.
     */
    if (bmpData) {
        ArrayObject *previous = (ArrayObject *)bmpData;
        free(previous->elements);
        free(previous);
        bmpData = nullptr;
    }

    bmpData = env->NewByteArray((jsize)pixels);
    if (!bmpData) {
        free(coverage);
        bmpWidth = bmpHeight = 0;
        return JNI_FALSE;
    }
    env->SetByteArrayRegion(bmpData, 0, (jsize)pixels, (const jbyte *)coverage);
    free(coverage);

    /* Tightly packed, so the pitch is the width. Android's ALPHA_8 rowBytes
     * was 4-byte aligned, which is exactly the mismatch that made
     * copyPixelsToBuffer overrun a w*h buffer on the Java side. */
    bmpPitch = bmpWidth;
    return JNI_TRUE;
}

static const FieldId RR3FontFields[] = {
    REGISTER_FIELD(RR3Font, ascent), REGISTER_FIELD(RR3Font, descent),
    REGISTER_FIELD(RR3Font, height), REGISTER_FIELD(RR3Font, top),
    REGISTER_FIELD(RR3Font, bottom), REGISTER_FIELD(RR3Font, leading),
    REGISTER_FIELD(RR3Font, glyphOffX), REGISTER_FIELD(RR3Font, glyphOffY),
    REGISTER_FIELD(RR3Font, glyphWidth), REGISTER_FIELD(RR3Font, glyphHeight),
    REGISTER_FIELD(RR3Font, glyphAdvance), REGISTER_FIELD(RR3Font, bmpLeft),
    REGISTER_FIELD(RR3Font, bmpTop), REGISTER_FIELD(RR3Font, bmpWidth),
    REGISTER_FIELD(RR3Font, bmpHeight), REGISTER_FIELD(RR3Font, bmpPitch),
    REGISTER_FIELD(RR3Font, bmpData), {NULL}
};

static jboolean font_init(JNIEnv *e, jobject o, jstring s, jboolean a, jboolean b, jfloat c, jfloat d)
{ return ((RR3Font *)o)->init(e, o, s, a, b, c, d); }
static jfloat font_getSize(JNIEnv *e, jobject o) { return ((RR3Font *)o)->getSize(e, o); }
static jboolean font_loadGlyph(JNIEnv *e, jobject o, jint c) { return ((RR3Font *)o)->loadGlyph(e, o, c); }
static jboolean font_loadBitmap(JNIEnv *e, jobject o, jint c, jfloat a, jfloat b, jfloat d, jint f)
{ return ((RR3Font *)o)->loadBitmap(e, o, c, a, b, d, f); }
static void font_ctor(JNIEnv *, jobject o, jclass) { new (o) RR3Font(); }

static const ManagedMethod RR3FontMethods[] = {
    ManagedMethod::RegisterNonVirtual<&font_ctor>(RR3Font::clazz, "<init>", "()V"),
    ManagedMethod::Register<&font_init>(RR3Font::clazz, "init", "(Ljava/lang/String;ZZFF)Z"),
    ManagedMethod::Register<&font_getSize>(RR3Font::clazz, "getSize", "()F"),
    ManagedMethod::Register<&font_loadGlyph>(RR3Font::clazz, "loadGlyph", "(I)Z"),
    ManagedMethod::Register<&font_loadBitmap>(RR3Font::clazz, "loadBitmap", "(IFFFI)Z"),
    {NULL}
};

Class RR3Font::clazz = {
    .classpath = "com/firemint/realracing3/Font",
    .classname = "Font",
    .managed_methods = RR3FontMethods,
    .native_methods = {NULL},
    .fields = RR3FontFields,
    .instance_size = sizeof(RR3Font),
};

static const int registered = ClassRegistry::register_class(RR3Font::clazz);

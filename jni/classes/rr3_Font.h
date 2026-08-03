#pragma once

#include "jni.h"
#include "jni_internals.h"

#include "rr3_font_face.h"

class RR3Font : public Object {
public:
    static Class clazz;
    Class *_getClass() { return &clazz; }

    jfloat ascent = 0, descent = 0, height = 0, top = 0, bottom = 0, leading = 0;
    jfloat glyphOffX = 0, glyphOffY = 0, glyphWidth = 0, glyphHeight = 0, glyphAdvance = 0;
    jint bmpLeft = 0, bmpTop = 0, bmpWidth = 0, bmpHeight = 0, bmpPitch = 0;
    jbyteArray bmpData = nullptr;

    jboolean init(JNIEnv *, jobject, jstring, jboolean, jboolean, jfloat, jfloat);
    jfloat getSize(JNIEnv *, jobject);
    jboolean loadGlyph(JNIEnv *, jobject, jint);
    jboolean loadBitmap(JNIEnv *, jobject, jint, jfloat, jfloat, jfloat, jint);

    /* Read by GlyphVector, which on Android reached the same three values
     * through getTypeface()/getSize()/getWidthScale(). */
    FontFace *face() const { return m_face; }
    float size() const { return m_size; }
    float width_scale() const { return m_width_scale; }

private:
    FontFace *m_face = nullptr;
    /*
     * Kept separate from `height`, which used to stand in for it.
     * Font.getSize() returns the em size the font was initialised with, and
     * height is ascent + descent - two numbers that only coincide for a face
     * whose metrics happen to sum to the em, which the previous synthetic
     * metrics (0.8 + 0.2) did by construction.
     */
    float m_size = 0.0f;
    float m_width_scale = 1.0f;
};

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Typeface loading, measurement and rasterisation for the two Firemint font
 * classes.
 *
 * On Android com/firemint/realracing3/Font and .../GlyphVector are thin
 * wrappers over Skia: Typeface.createFromFile parsed the outlines, Paint
 * produced the metrics and Canvas did the antialiased fill. libRealRacing3.so
 * itself contains no rasteriser at all - it exports no FT_* symbol and its
 * NEEDED list is fmodex/z/log/EGL/GLESv2/stdc++/m/c/dl - so all of that work
 * has to be supplied from this side or there is no text on screen.
 *
 * Sizes are em sizes in pixels, which is what Paint.setTextSize means, and
 * width_scale multiplies the horizontal axis the way Paint.setTextScaleX does.
 * The vertical axis is never scaled by it.
 */

typedef struct FontFace FontFace;

/*
 * Opens the typeface the engine named and returns a shared handle, or NULL if
 * the file is missing or cannot be parsed.
 *
 * The path arrives as an engine-side absolute name (/game/EurostileLTStd.otf)
 * and is translated with fix_path() exactly like every other asset open.
 *
 * Faces are cached by path and never released. That is deliberate on both
 * counts: one run opens myriadp0.otf twenty-six times and EurostileLTStd.otf
 * thirteen, so without the cache the file would be resident once per Font
 * instance, and the thirteen faces together are about a megabyte that stays
 * reachable for as long as the game is running anyway.
 */
FontFace *font_face_open(const char *engine_path);

/*
 * Paint.FontMetrics, in the sign convention com/firemint/realracing3/Font
 * publishes to native (Font.java:84): ascent and top are negated so both come
 * out positive, descent and bottom stay positive.
 */
typedef struct {
    float ascent, descent, height, top, bottom, leading;
} FontVMetrics;

void font_face_vmetrics(FontFace *face, float px_size, FontVMetrics *out);

/*
 * The integer line box android.text.StaticLayout would use.
 *
 * GlyphVector builds every layout with includePad=false (the trailing `false`
 * at GlyphVector.java:188), and StaticLayout.out() only substitutes the
 * top/bottom extents for the first and last line when includePad is true. So
 * every line, first and last included, is ascent..descent tall - not
 * top..bottom - and the values are the rounded FontMetricsInt ones.
 */
void font_face_line_box(FontFace *face, float px_size, int *baseline,
                        int *line_height);

/*
 * Ink box of a run of text, relative to a pen origin sitting on the baseline
 * at x = 0. y grows downwards, so y0 is negative for anything with an
 * ascender - the same convention as the Rect that Paint.getTextBounds fills.
 */
typedef struct {
    int   x0, y0, x1, y1;
    float advance;
    int   has_ink;
} TextInk;

void font_face_measure(FontFace *face, const char *utf8, size_t bytes,
                       float px_size, float width_scale, TextInk *out);

/*
 * Antialiased coverage for the same run, max-blended into an 8-bit buffer.
 *
 * Max rather than add because glyphs whose ink boxes overlap (italics, tight
 * kerning pairs) would otherwise saturate past 255 and produce a bright seam
 * where the two coverages meet.
 */
void font_face_draw(FontFace *face, const char *utf8, size_t bytes,
                    float px_size, float width_scale,
                    unsigned char *dst, int dst_w, int dst_h, int dst_stride,
                    float pen_x, float baseline_y);

/* Width of a run without its ink box - the hot path for word wrapping. */
float font_face_advance(FontFace *face, const char *utf8, size_t bytes,
                        float px_size, float width_scale);

/*
 * Encodes one codepoint as UTF-8 into buf (which must hold at least 5 bytes)
 * and returns the byte count, so the single-glyph entry points in Font can
 * reuse the run-based ones above.
 */
size_t font_face_encode_utf8(uint32_t codepoint, char *buf);

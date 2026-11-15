/* F0 read-only BGRA observations. No rendering or allocation policy lives here. */
#ifndef _SWEET_FB_TRACE_H_
#define _SWEET_FB_TRACE_H_
#include <stdint.h>

struct SweetFBTraceView {
	const volatile uint8_t *base;
	uint64_t length, offset;
	uint32_t width, height, stride;
};
struct SweetFBTracePixels {
	uint64_t hash;
	uint32_t samples, rgb, alpha, differentRGB, differentAlpha;
};

/* Validate every coordinate and the last byte before touching either mapping. */
static inline int
sweet_fb_trace_bounds(const struct SweetFBTraceView *v,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	if (!v->base || !w || !h || !v->width || !v->height ||
	    v->width > 16384 || v->height > 16384 ||
	    v->stride < (uint64_t)v->width * 4 ||
	    x >= v->width || y >= v->height || w > v->width - x || h > v->height - y ||
	    v->offset > v->length) {
		return 0;
	}
	uint64_t end = (uint64_t)(y + h - 1) * v->stride + (uint64_t)(x + w) * 4;
	return end <= v->length - v->offset &&
	       v->length <= (uint64_t)UINTPTR_MAX - (uintptr_t)v->base;
}

/* A complete BGRA word at the same destination pixel coordinates in both views.
 * Stride padding is excluded. Step=8 bounds cost, so this remains a sample,
 * never proof that unsampled pixels (or an entire glyph) are absent. */
static inline int
sweet_fb_trace_pixels(const struct SweetFBTraceView *v, uint32_t x, uint32_t y,
    const struct SweetFBTraceView *other, uint32_t ox, uint32_t oy,
    uint32_t w, uint32_t h, uint32_t step, struct SweetFBTracePixels *out)
{
	*out = (struct SweetFBTracePixels){1469598103934665603ULL, 0, 0, 0, 0, 0};
	if (!step || step > 16384 || !sweet_fb_trace_bounds(v, x, y, w, h) ||
	    (other && !sweet_fb_trace_bounds(other, ox, oy, w, h))) {
		return 0;
	}
	for (uint32_t row = 0; row < h; row += step) {
		for (uint32_t col = 0; col < w; col += step) {
			uint64_t at = v->offset + (uint64_t)(y + row) * v->stride + (uint64_t)(x + col) * 4;
			uint32_t pixel = 0, compare = 0;
			for (uint32_t c = 0; c < 4; c++) {
				uint8_t b = v->base[at + c];
				pixel |= (uint32_t)b << (c * 8);
				out->hash = (out->hash ^ b) * 1099511628211ULL;
			}
			++out->samples;
			out->rgb += (pixel & 0xffffffu) != 0;
			out->alpha += (pixel >> 24) != 0;
			if (other) {
				uint64_t there = other->offset + (uint64_t)(oy + row) * other->stride +
				    (uint64_t)(ox + col) * 4;
				for (uint32_t c = 0; c < 4; c++) {
					compare |= (uint32_t)other->base[there + c] << (c * 8);
				}
				out->differentRGB += ((pixel ^ compare) & 0xffffffu) != 0;
				out->differentAlpha += ((pixel ^ compare) >> 24) != 0;
			}
		}
	}
	return 1;
}
#endif

/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_WLR_RENDERER_H
#define WLR_RENDER_WLR_RENDERER_H

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/box.h>

struct wlr_backend;
struct wlr_renderer_impl;
struct wlr_drm_format_set;
struct wlr_buffer;
struct wlr_box;
struct wlr_fbox;

/**
 * A renderer for basic 2D operations.
 */
struct wlr_renderer {
	struct {
		struct wl_signal destroy;
		/**
		 * Emitted when the GPU is lost, e.g. on GPU reset.
		 *
		 * Compositors should destroy the renderer and re-create it.
		 */
		struct wl_signal lost;
	} events;

	// private state

	const struct wlr_renderer_impl *impl;

	bool rendering;
	bool rendering_with_buffer;
};

/**
 * Automatically create a new renderer.
 *
 * Selects an appropriate renderer type to use depending on the backend,
 * platform, environment, etc.
 */
struct wlr_renderer *wlr_renderer_autocreate(struct wlr_backend *backend);

/**
 * Start a render pass with the provided viewport.
 *
 * This should be called after wlr_output_attach_render(). Compositors must call
 * wlr_renderer_end() when they are done.
 *
 * Returns false on failure, in which case compositors shouldn't try rendering.
 */
bool wlr_renderer_begin(struct wlr_renderer *r, uint32_t width, uint32_t height);
/**
 * Start a render pass on the provided struct wlr_buffer.
 *
 * Compositors must call wlr_renderer_end() when they are done.
 */
bool wlr_renderer_begin_with_buffer(struct wlr_renderer *r,
	struct wlr_buffer *buffer);
/**
 * End a render pass.
 */
void wlr_renderer_end(struct wlr_renderer *r);
/**
 * Clear the viewport with the provided color.
 */
void wlr_renderer_clear(struct wlr_renderer *r, const float color[static 4]);
/**
 * Defines a scissor box. Only pixels that lie within the scissor box can be
 * modified by drawing functions. Providing a NULL `box` disables the scissor
 * box.
 */
void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box);
/**
 * Renders the requested texture.
 */
bool wlr_render_texture(struct wlr_renderer *r, struct wlr_texture *texture,
	const float projection[static 9], int x, int y, float alpha);
/**
 * Renders the requested texture using the provided matrix.
 */
bool wlr_render_texture_with_matrix(struct wlr_renderer *r,
	struct wlr_texture *texture, const float matrix[static 9], float alpha);
/**
 * Renders the requested texture using the provided matrix, after cropping it
 * to the provided rectangle.
 */
bool wlr_render_subtexture_with_matrix(struct wlr_renderer *r,
	struct wlr_texture *texture, const struct wlr_fbox *box,
	const float matrix[static 9], float alpha);
/**
 * Renders a solid rectangle in the specified color.
 */
void wlr_render_rect(struct wlr_renderer *r, const struct wlr_box *box,
	const float color[static 4], const float projection[static 9]);
/**
 * Renders a solid quadrangle in the specified color with the specified matrix.
 */
void wlr_render_quad_with_matrix(struct wlr_renderer *r,
	const float color[static 4], const float matrix[static 9]);
/**
 * Get the shared-memory formats supporting import usage. Buffers allocated
 * with a format from this list may be imported via wlr_texture_from_pixels().
 */
const uint32_t *wlr_renderer_get_shm_texture_formats(
	struct wlr_renderer *r, size_t *len);
/**
 * Get the DMA-BUF formats supporting sampling usage. Buffers allocated with
 * a format from this list may be imported via wlr_texture_from_dmabuf().
 */
const struct wlr_drm_format_set *wlr_renderer_get_dmabuf_texture_formats(
	struct wlr_renderer *renderer);
/**
 * Reads out of pixels of the currently bound surface into data. `stride` is in
 * bytes.
 */
bool wlr_renderer_read_pixels(struct wlr_renderer *r, uint32_t fmt,
	uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, void *data);

/**
 * Initializes wl_shm, linux-dmabuf and other buffer factory protocols.
 *
 * Returns false on failure.
 */
bool wlr_renderer_init_wl_display(struct wlr_renderer *r,
	struct wl_display *wl_display);

/**
 * Initializes wl_shm on the provided struct wl_display.
 */
bool wlr_renderer_init_wl_shm(struct wlr_renderer *r,
	struct wl_display *wl_display);

/**
 * Obtains the FD of the DRM device used for rendering, or -1 if unavailable.
 *
 * The caller doesn't have ownership of the FD, it must not close it.
 */
int wlr_renderer_get_drm_fd(struct wlr_renderer *r);

/**
 * Destroys the renderer.
 *
 * Textures must be destroyed separately.
 */
void wlr_renderer_destroy(struct wlr_renderer *renderer);

/**
 * A render pass accumulates drawing operations until submitted to the GPU.
 */
struct wlr_render_pass;

/**
 * An object that can be queried after a render to get the duration of the render.
 */
struct wlr_render_timer;

struct wlr_buffer_pass_options {
	/* Timer to measure the duration of the render pass */
	struct wlr_render_timer *timer;
};

/**
 * Begin a new render pass with the supplied destination buffer.
 *
 * Callers must call wlr_render_pass_submit() once they are done with the
 * render pass.
 */
struct wlr_render_pass *wlr_renderer_begin_buffer_pass(struct wlr_renderer *renderer,
	struct wlr_buffer *buffer, const struct wlr_buffer_pass_options *options);

/**
 * Submit the render pass.
 *
 * The render pass cannot be used after this function is called.
 */
bool wlr_render_pass_submit(struct wlr_render_pass *render_pass);

/**
 * Blend modes.
 */
enum wlr_render_blend_mode {
	/* Pre-multiplied alpha (default) */
	WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
	/* Blending is disabled */
	WLR_RENDER_BLEND_MODE_NONE,
};

/**
 * Filter modes.
 */
enum wlr_scale_filter_mode {
	/* bilinear texture filtering (default) */
	WLR_SCALE_FILTER_BILINEAR,
	/* nearest texture filtering */
	WLR_SCALE_FILTER_NEAREST,
};

struct wlr_render_texture_options {
	/* Source texture */
	struct wlr_texture *texture;
	/* Source coordinates, leave empty to render the whole texture */
	struct wlr_fbox src_box;
	/* Destination coordinates, width/height default to the texture size */
	struct wlr_box dst_box;
	/* Opacity between 0 (transparent) and 1 (opaque), leave NULL for opaque */
	const float *alpha;
	/* Clip region, leave NULL to disable clipping */
	const pixman_region32_t *clip;
	/* Transform applied to the source texture */
	enum wl_output_transform transform;
	/* Filtering */
	enum wlr_scale_filter_mode filter_mode;
	/* Blend mode */
	enum wlr_render_blend_mode blend_mode;
};

/**
 * Render a texture.
 */
void wlr_render_pass_add_texture(struct wlr_render_pass *render_pass,
	const struct wlr_render_texture_options *options);

/**
 * A color value.
 *
 * Each channel has values between 0 and 1 inclusive. The R, G, B
 * channels need to be pre-multiplied by A.
 */
struct wlr_render_color {
	float r, g, b, a;
};

struct wlr_render_rect_options {
	/* Rectangle coordinates */
	struct wlr_box box;
	/* Source color */
	struct wlr_render_color color;
	/* Clip region, leave NULL to disable clipping */
	const pixman_region32_t *clip;
	/* Blend mode */
	enum wlr_render_blend_mode blend_mode;
};

/**
 * Render a rectangle.
 */
void wlr_render_pass_add_rect(struct wlr_render_pass *render_pass,
	const struct wlr_render_rect_options *options);

/**
 * Allocate and initialise a new render timer.
 */
struct wlr_render_timer *wlr_render_timer_create(struct wlr_renderer *renderer);

/**
 * Get the render duration in nanoseconds from the timer.
 *
 * Returns -1 if the duration is unavailable.
 */
int wlr_render_timer_get_duration_ns(struct wlr_render_timer *timer);

/**
 * Destroy the render timer.
 */
void wlr_render_timer_destroy(struct wlr_render_timer *timer);

#endif

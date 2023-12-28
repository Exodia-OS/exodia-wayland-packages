#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/headless.h"

struct wlr_headless_backend *headless_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_headless(wlr_backend));
	struct wlr_headless_backend *backend = wl_container_of(wlr_backend, backend, backend);
	return backend;
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);
	wlr_log(WLR_INFO, "Starting headless backend");

	struct wlr_headless_output *output;
	wl_list_for_each(output, &backend->outputs, link) {
		wl_signal_emit_mutable(&backend->backend.events.new_output,
			&output->wlr_output);
	}

	backend->started = true;
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);
	if (!wlr_backend) {
		return;
	}

	wlr_backend_finish(wlr_backend);

	struct wlr_headless_output *output, *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &backend->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	wl_list_remove(&backend->display_destroy.link);

	free(backend);
}

static uint32_t get_buffer_caps(struct wlr_backend *wlr_backend) {
	return WLR_BUFFER_CAP_DATA_PTR
		| WLR_BUFFER_CAP_DMABUF
		| WLR_BUFFER_CAP_SHM;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_buffer_caps = get_buffer_caps,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_headless_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_headless_backend_create(struct wl_display *display) {
	wlr_log(WLR_INFO, "Creating headless backend");

	struct wlr_headless_backend *backend =
		calloc(1, sizeof(struct wlr_headless_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_backend");
		return NULL;
	}

	wlr_backend_init(&backend->backend, &backend_impl);

	backend->display = display;
	wl_list_init(&backend->outputs);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	return &backend->backend;
}

bool wlr_backend_is_headless(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}

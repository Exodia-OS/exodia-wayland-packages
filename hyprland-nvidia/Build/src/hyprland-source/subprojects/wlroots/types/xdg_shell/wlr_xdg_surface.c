#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "types/wlr_xdg_shell.h"

struct wlr_xdg_surface *wlr_xdg_surface_try_from_wlr_surface(
		struct wlr_surface *surface) {
	if (surface->role != &xdg_toplevel_surface_role &&
			surface->role != &xdg_popup_surface_role) {
		return NULL;
	}
	if (surface->role_resource == NULL) {
		return NULL;
	}
	return wlr_xdg_surface_from_resource(surface->role_resource);
}

static void xdg_surface_configure_destroy(
		struct wlr_xdg_surface_configure *configure) {
	if (configure == NULL) {
		return;
	}
	wl_list_remove(&configure->link);
	free(configure->toplevel_configure);
	free(configure);
}

static void reset_xdg_surface(struct wlr_xdg_surface *surface) {
	surface->configured = false;

	struct wlr_xdg_popup *popup, *popup_tmp;
	wl_list_for_each_safe(popup, popup_tmp, &surface->popups, link) {
		wlr_xdg_popup_destroy(popup);
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		reset_xdg_toplevel(surface->toplevel);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		reset_xdg_popup(surface->popup);
		break;
	case WLR_XDG_SURFACE_ROLE_NONE:
		break;
	}

	struct wlr_xdg_surface_configure *configure, *tmp;
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		xdg_surface_configure_destroy(configure);
	}

	if (surface->configure_idle) {
		wl_event_source_remove(surface->configure_idle);
		surface->configure_idle = NULL;
	}
}

static void xdg_surface_handle_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);
	if (surface == NULL) {
		return;
	}

	if (surface->role == WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}

	// First find the ack'ed configure
	bool found = false;
	struct wlr_xdg_surface_configure *configure, *tmp;
	wl_list_for_each(configure, &surface->configure_list, link) {
		if (configure->serial == serial) {
			found = true;
			break;
		}
	}
	if (!found) {
		wl_resource_post_error(surface->client->resource,
			XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
			"wrong configure serial: %u", serial);
		return;
	}
	// Then remove old configures from the list
	wl_list_for_each_safe(configure, tmp, &surface->configure_list, link) {
		if (configure->serial == serial) {
			break;
		}
		wl_signal_emit_mutable(&surface->events.ack_configure, configure);
		xdg_surface_configure_destroy(configure);
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		handle_xdg_toplevel_ack_configure(surface->toplevel,
			configure->toplevel_configure);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		handle_xdg_popup_ack_configure(surface->popup,
			configure->popup_configure);
		break;
	}

	surface->configured = true;
	surface->pending.configure_serial = serial;

	wl_signal_emit_mutable(&surface->events.ack_configure, configure);
	xdg_surface_configure_destroy(configure);
}

static void surface_send_configure(void *user_data) {
	struct wlr_xdg_surface *surface = user_data;

	surface->configure_idle = NULL;

	struct wlr_xdg_surface_configure *configure =
		calloc(1, sizeof(struct wlr_xdg_surface_configure));
	if (configure == NULL) {
		wl_client_post_no_memory(surface->client->client);
		return;
	}

	wl_list_insert(surface->configure_list.prev, &configure->link);
	configure->serial = surface->scheduled_serial;
	configure->surface = surface;

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		assert(0 && "not reached");
		break;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		configure->toplevel_configure =
			send_xdg_toplevel_configure(surface->toplevel);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		configure->popup_configure =
			send_xdg_popup_configure(surface->popup);
		break;
	}

	wl_signal_emit_mutable(&surface->events.configure, configure);

	xdg_surface_send_configure(surface->resource, configure->serial);
}

uint32_t wlr_xdg_surface_schedule_configure(struct wlr_xdg_surface *surface) {
	struct wl_display *display = wl_client_get_display(surface->client->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);

	if (surface->configure_idle == NULL) {
		surface->scheduled_serial = wl_display_next_serial(display);
		surface->configure_idle = wl_event_loop_add_idle(loop,
			surface_send_configure, surface);
		if (surface->configure_idle == NULL) {
			wl_client_post_no_memory(surface->client->client);
		}
	}
	return surface->scheduled_serial;
}

static void xdg_surface_handle_get_popup(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *parent_resource,
		struct wl_resource *positioner_resource) {
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_from_resource(resource);
	struct wlr_xdg_surface *parent = NULL;
	if (parent_resource != NULL) {
		parent = wlr_xdg_surface_from_resource(parent_resource);
	}
	if (xdg_surface == NULL) {
		return; // TODO: create an inert xdg_popup
	}
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(positioner_resource);
	create_xdg_popup(xdg_surface, parent, positioner, id);
}

static void xdg_surface_handle_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_from_resource(resource);
	if (xdg_surface == NULL) {
		return; // TODO: create an inert xdg_toplevel
	}
	create_xdg_toplevel(xdg_surface, id);
}

static void xdg_surface_handle_set_window_geometry(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);
	if (surface == NULL) {
		return;
	}

	if (surface->role == WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}

	if (width <= 0 || height <= 0) {
		wl_resource_post_error(resource,
			XDG_SURFACE_ERROR_INVALID_SIZE,
			"Tried to set invalid xdg-surface geometry");
		return;
	}

	surface->pending.geometry.x = x;
	surface->pending.geometry.y = y;
	surface->pending.geometry.width = width;
	surface->pending.geometry.height = height;
}

static void xdg_surface_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_from_resource(resource);
	if (surface == NULL) {
		return;
	}

	if (surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
		wl_resource_post_error(resource,
			XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
			"surface was destroyed before its role object");
		return;
	}

	wl_resource_destroy(resource);
}

static const struct xdg_surface_interface xdg_surface_implementation = {
	.destroy = xdg_surface_handle_destroy,
	.get_toplevel = xdg_surface_handle_get_toplevel,
	.get_popup = xdg_surface_handle_get_popup,
	.ack_configure = xdg_surface_handle_ack_configure,
	.set_window_geometry = xdg_surface_handle_set_window_geometry,
};

static void xdg_surface_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_xdg_surface *surface =
		wlr_xdg_surface_from_resource(resource);
	if (surface != NULL) {
		wlr_surface_destroy_role_object(surface->surface);
	}
}

static void xdg_surface_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_surface *surface =
		wl_container_of(listener, surface, surface_commit);

	if (wlr_surface_has_buffer(surface->surface) && !surface->configured) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface has never been configured");
		return;
	}

	// surface->role might be NONE for inert popups
	// So we check surface->surface->role
	if (surface->surface->role == NULL) {
		wl_resource_post_error(surface->resource,
			XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
			"xdg_surface must have a role");
		return;
	}
}

void xdg_surface_role_commit(struct wlr_surface *wlr_surface) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_try_from_wlr_surface(wlr_surface);
	assert(surface != NULL);

	surface->current = surface->pending;

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_NONE:
		// inert toplevel or popup
		return;
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		handle_xdg_toplevel_committed(surface->toplevel);
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		handle_xdg_popup_committed(surface->popup);
		break;
	}

	if (!surface->added) {
		surface->added = true;
		wl_signal_emit_mutable(&surface->client->shell->events.new_surface,
			surface);
	}

	if (surface->configured && wlr_surface_has_buffer(wlr_surface)) {
		wlr_surface_map(wlr_surface);
	}
}

void xdg_surface_role_unmap(struct wlr_surface *wlr_surface) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_try_from_wlr_surface(wlr_surface);
	assert(surface != NULL);

	reset_xdg_surface(surface);
}

void xdg_surface_role_destroy(struct wlr_surface *wlr_surface) {
	struct wlr_xdg_surface *surface = wlr_xdg_surface_try_from_wlr_surface(wlr_surface);
	assert(surface != NULL);

	destroy_xdg_surface_role_object(surface);

	wl_list_remove(&surface->link);
	wl_list_remove(&surface->surface_commit.link);

	wl_resource_set_user_data(surface->resource, NULL);
	free(surface);
}

struct wlr_xdg_surface *create_xdg_surface(
		struct wlr_xdg_client *client, struct wlr_surface *wlr_surface,
		uint32_t id) {
	struct wlr_xdg_surface *surface =
		calloc(1, sizeof(struct wlr_xdg_surface));
	if (surface == NULL) {
		wl_client_post_no_memory(client->client);
		return NULL;
	}

	surface->client = client;
	surface->role = WLR_XDG_SURFACE_ROLE_NONE;
	surface->surface = wlr_surface;
	surface->resource = wl_resource_create(client->client,
		&xdg_surface_interface, wl_resource_get_version(client->resource),
		id);
	if (surface->resource == NULL) {
		free(surface);
		wl_client_post_no_memory(client->client);
		return NULL;
	}

	if (wlr_surface_has_buffer(surface->surface)) {
		wl_resource_destroy(surface->resource);
		free(surface);
		wl_resource_post_error(client->resource,
			XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
			"xdg_surface must not have a buffer at creation");
		return NULL;
	}

	wl_list_init(&surface->configure_list);
	wl_list_init(&surface->popups);

	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.ping_timeout);
	wl_signal_init(&surface->events.new_popup);
	wl_signal_init(&surface->events.configure);
	wl_signal_init(&surface->events.ack_configure);

	wl_signal_add(&surface->surface->events.commit,
		&surface->surface_commit);
	surface->surface_commit.notify = xdg_surface_handle_surface_commit;

	wlr_log(WLR_DEBUG, "new xdg_surface %p (res %p)", surface,
		surface->resource);
	wl_resource_set_implementation(surface->resource,
		&xdg_surface_implementation, surface,
		xdg_surface_handle_resource_destroy);
	wl_list_insert(&client->surfaces, &surface->link);

	return surface;
}

void destroy_xdg_surface_role_object(struct wlr_xdg_surface *surface) {
	if (surface->surface->mapped) {
		wlr_surface_unmap(surface->surface);
	} else {
		reset_xdg_surface(surface);
	}

	if (surface->added) {
		wl_signal_emit_mutable(&surface->events.destroy, NULL);
		surface->added = false;
	}

	switch (surface->role) {
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		destroy_xdg_toplevel(surface->toplevel);
		surface->toplevel = NULL;
		break;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		destroy_xdg_popup(surface->popup);
		surface->popup = NULL;
		break;
	case WLR_XDG_SURFACE_ROLE_NONE:
		// This space is intentionally left blank
		break;
	}

	surface->role = WLR_XDG_SURFACE_ROLE_NONE;
}

struct wlr_xdg_surface *wlr_xdg_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_surface_interface,
		&xdg_surface_implementation));
	return wl_resource_get_user_data(resource);
}

void wlr_xdg_surface_ping(struct wlr_xdg_surface *surface) {
	if (surface->client->ping_serial != 0) {
		// already pinged
		return;
	}

	surface->client->ping_serial =
		wl_display_next_serial(wl_client_get_display(surface->client->client));
	wl_event_source_timer_update(surface->client->ping_timer,
		surface->client->shell->ping_timeout);
	xdg_wm_base_send_ping(surface->client->resource,
		surface->client->ping_serial);
}

void wlr_xdg_popup_get_position(struct wlr_xdg_popup *popup,
		double *popup_sx, double *popup_sy) {
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
	assert(parent != NULL);
	struct wlr_box parent_geo;
	wlr_xdg_surface_get_geometry(parent, &parent_geo);
	*popup_sx = parent_geo.x + popup->current.geometry.x -
		popup->base->current.geometry.x;
	*popup_sy = parent_geo.y + popup->current.geometry.y -
		popup->base->current.geometry.y;
}

struct wlr_surface *wlr_xdg_surface_surface_at(
		struct wlr_xdg_surface *surface, double sx, double sy,
		double *sub_x, double *sub_y) {
	struct wlr_surface *sub = wlr_xdg_surface_popup_surface_at(surface, sx, sy,
			sub_x, sub_y);
	if (sub != NULL) {
		return sub;
	}
	return wlr_surface_surface_at(surface->surface, sx, sy, sub_x, sub_y);
}

struct wlr_surface *wlr_xdg_surface_popup_surface_at(
		struct wlr_xdg_surface *surface, double sx, double sy,
		double *sub_x, double *sub_y) {
	struct wlr_xdg_popup *popup;
	wl_list_for_each(popup, &surface->popups, link) {
		if (!popup->base->surface->mapped) {
			continue;
		}

		double popup_sx, popup_sy;
		wlr_xdg_popup_get_position(popup, &popup_sx, &popup_sy);

		struct wlr_surface *sub = wlr_xdg_surface_surface_at(
			popup->base, sx - popup_sx, sy - popup_sy,
			sub_x, sub_y);
		if (sub != NULL) {
			return sub;
		}
	}

	return NULL;
}

struct xdg_surface_iterator_data {
	wlr_surface_iterator_func_t user_iterator;
	void *user_data;
	int x, y;
};

static void xdg_surface_iterator(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct xdg_surface_iterator_data *iter_data = data;
	iter_data->user_iterator(surface, iter_data->x + sx, iter_data->y + sy,
		iter_data->user_data);
}

static void xdg_surface_for_each_popup_surface(struct wlr_xdg_surface *surface,
		int x, int y, wlr_surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xdg_popup *popup;
	wl_list_for_each(popup, &surface->popups, link) {
		if (!popup->base->surface->mapped) {
			continue;
		}

		double popup_sx, popup_sy;
		wlr_xdg_popup_get_position(popup, &popup_sx, &popup_sy);

		struct xdg_surface_iterator_data data = {
			.user_iterator = iterator,
			.user_data = user_data,
			.x = x + popup_sx, .y = y + popup_sy,
		};
		wlr_surface_for_each_surface(popup->base->surface,
			xdg_surface_iterator, &data);

		xdg_surface_for_each_popup_surface(popup->base,
			x + popup_sx, y + popup_sy, iterator, user_data);
	}
}

void wlr_xdg_surface_for_each_surface(struct wlr_xdg_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	wlr_surface_for_each_surface(surface->surface, iterator, user_data);
	xdg_surface_for_each_popup_surface(surface, 0, 0, iterator, user_data);
}

void wlr_xdg_surface_for_each_popup_surface(struct wlr_xdg_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	xdg_surface_for_each_popup_surface(surface, 0, 0, iterator, user_data);
}

void wlr_xdg_surface_get_geometry(struct wlr_xdg_surface *surface,
		struct wlr_box *box) {
	wlr_surface_get_extends(surface->surface, box);

	/* The client never set the geometry */
	if (wlr_box_empty(&surface->current.geometry)) {
		return;
	}

	wlr_box_intersection(box, &surface->current.geometry, box);
}

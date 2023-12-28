#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include "types/wlr_region.h"
#include "types/wlr_subcompositor.h"

// Note: wl_subsurface becomes inert on parent surface destroy

#define SUBCOMPOSITOR_VERSION 1

static bool subsurface_is_synchronized(struct wlr_subsurface *subsurface) {
	while (subsurface != NULL) {
		if (subsurface->synchronized) {
			return true;
		}

		subsurface = wlr_subsurface_try_from_wlr_surface(subsurface->parent);
	}

	return false;
}

static const struct wl_subsurface_interface subsurface_implementation;

static void subsurface_destroy(struct wlr_subsurface *subsurface) {
	if (subsurface->has_cache) {
		wlr_surface_unlock_cached(subsurface->surface, subsurface->cached_seq);
	}

	wl_signal_emit_mutable(&subsurface->events.destroy, subsurface);

	wl_list_remove(&subsurface->surface_client_commit.link);
	wl_list_remove(&subsurface->current.link);
	wl_list_remove(&subsurface->pending.link);
	wl_list_remove(&subsurface->parent_destroy.link);

	wl_resource_set_user_data(subsurface->resource, NULL);
	free(subsurface);
}

/**
 * Get a wlr_subsurface from a wl_subsurface resource.
 *
 * Returns NULL if the subsurface is inert (e.g. the wl_surface object or the
 * parent surface got destroyed).
 */
static struct wlr_subsurface *subsurface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_subsurface_interface,
		&subsurface_implementation));
	return wl_resource_get_user_data(resource);
}

static void subsurface_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void subsurface_handle_set_position(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	subsurface->pending.x = x;
	subsurface->pending.y = y;
}

static struct wlr_subsurface *subsurface_find_sibling(
		struct wlr_subsurface *subsurface, struct wlr_surface *surface) {
	struct wlr_surface *parent = subsurface->parent;

	struct wlr_subsurface *sibling;
	wl_list_for_each(sibling, &parent->pending.subsurfaces_below, pending.link) {
		if (sibling->surface == surface && sibling != subsurface) {
			return sibling;
		}
	}
	wl_list_for_each(sibling, &parent->pending.subsurfaces_above, pending.link) {
		if (sibling->surface == surface && sibling != subsurface) {
			return sibling;
		}
	}

	return NULL;
}

static void subsurface_handle_place_above(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling_resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	struct wlr_surface *sibling_surface =
		wlr_surface_from_resource(sibling_resource);

	struct wl_list *node;
	if (sibling_surface == subsurface->parent) {
		node = &subsurface->parent->pending.subsurfaces_above;
	} else {
		struct wlr_subsurface *sibling =
			subsurface_find_sibling(subsurface, sibling_surface);
		if (!sibling) {
			wl_resource_post_error(subsurface->resource,
				WL_SUBSURFACE_ERROR_BAD_SURFACE,
				"%s: wl_surface@%" PRIu32 "is not a parent or sibling",
				"place_above", wl_resource_get_id(sibling_resource));
			return;
		}
		node = &sibling->pending.link;
	}

	wl_list_remove(&subsurface->pending.link);
	wl_list_insert(node, &subsurface->pending.link);

	subsurface->reordered = true;
}

static void subsurface_handle_place_below(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling_resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	struct wlr_surface *sibling_surface =
		wlr_surface_from_resource(sibling_resource);

	struct wl_list *node;
	if (sibling_surface == subsurface->parent) {
		node = &subsurface->parent->pending.subsurfaces_below;
	} else {
		struct wlr_subsurface *sibling =
			subsurface_find_sibling(subsurface, sibling_surface);
		if (!sibling) {
			wl_resource_post_error(subsurface->resource,
				WL_SUBSURFACE_ERROR_BAD_SURFACE,
				"%s: wl_surface@%" PRIu32 " is not a parent or sibling",
				"place_below", wl_resource_get_id(sibling_resource));
			return;
		}
		node = &sibling->pending.link;
	}

	wl_list_remove(&subsurface->pending.link);
	wl_list_insert(node->prev, &subsurface->pending.link);

	subsurface->reordered = true;
}

static void subsurface_handle_set_sync(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	subsurface->synchronized = true;
}

static void subsurface_handle_set_desync(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	if (subsurface->synchronized) {
		subsurface->synchronized = false;

		if (!subsurface_is_synchronized(subsurface) &&
				subsurface->has_cache) {
			wlr_surface_unlock_cached(subsurface->surface,
				subsurface->cached_seq);
			subsurface->has_cache = false;
		}
	}
}

static const struct wl_subsurface_interface subsurface_implementation = {
	.destroy = subsurface_handle_destroy,
	.set_position = subsurface_handle_set_position,
	.place_above = subsurface_handle_place_above,
	.place_below = subsurface_handle_place_below,
	.set_sync = subsurface_handle_set_sync,
	.set_desync = subsurface_handle_set_desync,
};

const struct wlr_surface_role subsurface_role;

void subsurface_consider_map(struct wlr_subsurface *subsurface) {
	if (subsurface->parent->mapped && wlr_surface_has_buffer(subsurface->surface)) {
		wlr_surface_map(subsurface->surface);
	}
}

static void subsurface_role_commit(struct wlr_surface *surface) {
	struct wlr_subsurface *subsurface = wlr_subsurface_try_from_wlr_surface(surface);
	if (subsurface == NULL) {
		return;
	}

	subsurface_consider_map(subsurface);
}

static void subsurface_role_destroy(struct wlr_surface *surface) {
	struct wlr_subsurface *subsurface = wlr_subsurface_try_from_wlr_surface(surface);
	if (subsurface == NULL) {
		return;
	}

	subsurface_destroy(subsurface);
}

const struct wlr_surface_role subsurface_role = {
	.name = "wl_subsurface",
	.commit = subsurface_role_commit,
	.destroy = subsurface_role_destroy,
};

static void subsurface_handle_parent_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_subsurface *subsurface =
		wl_container_of(listener, subsurface, parent_destroy);
	// Once the parent is destroyed, the client has no way to use the
	// wl_subsurface object anymore, so we can destroy it.
	subsurface_destroy(subsurface);
}

static void subsurface_handle_surface_client_commit(
		struct wl_listener *listener, void *data) {
	struct wlr_subsurface *subsurface =
		wl_container_of(listener, subsurface, surface_client_commit);
	struct wlr_surface *surface = subsurface->surface;

	if (subsurface_is_synchronized(subsurface)) {
		if (subsurface->has_cache) {
			// We already lock a previous commit. The prevents any future
			// commit to be applied before we release the previous commit.
			return;
		}
		subsurface->has_cache = true;
		subsurface->cached_seq = wlr_surface_lock_pending(surface);
	} else if (subsurface->has_cache) {
		wlr_surface_unlock_cached(surface, subsurface->cached_seq);
		subsurface->has_cache = false;
	}
}

static void collect_damage_iter(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct wlr_subsurface *subsurface = data;
	pixman_region32_t *damage = &subsurface->parent->external_damage;
	pixman_region32_union_rect(damage, damage,
		subsurface->current.x + sx,
		subsurface->current.y + sy,
		surface->current.width, surface->current.height);
}

void subsurface_handle_parent_commit(struct wlr_subsurface *subsurface) {
	struct wlr_surface *surface = subsurface->surface;

	bool moved = subsurface->current.x != subsurface->pending.x ||
		subsurface->current.y != subsurface->pending.y;
	if (subsurface->surface->mapped && moved) {
		wlr_surface_for_each_surface(surface,
			collect_damage_iter, subsurface);
	}

	if (subsurface->synchronized && subsurface->has_cache) {
		wlr_surface_unlock_cached(surface, subsurface->cached_seq);
		subsurface->has_cache = false;
	}

	subsurface->current.x = subsurface->pending.x;
	subsurface->current.y = subsurface->pending.y;
	if (subsurface->surface->mapped && (moved || subsurface->reordered)) {
		subsurface->reordered = false;
		wlr_surface_for_each_surface(surface,
			collect_damage_iter, subsurface);
	}

	if (!subsurface->added) {
		subsurface->added = true;
		wl_signal_emit_mutable(&subsurface->parent->events.new_subsurface,
			subsurface);
		subsurface_consider_map(subsurface);
	}
}

struct wlr_subsurface *wlr_subsurface_try_from_wlr_surface(struct wlr_surface *surface) {
	if (surface->role != &subsurface_role || surface->role_resource == NULL) {
		return NULL;
	}
	return subsurface_from_resource(surface->role_resource);
}

static void subcompositor_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void subcompositor_handle_get_subsurface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource,
		struct wl_resource *parent_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct wlr_surface *parent = wlr_surface_from_resource(parent_resource);

	struct wlr_subsurface *subsurface = calloc(1, sizeof(struct wlr_subsurface));
	if (!subsurface) {
		wl_client_post_no_memory(client);
		return;
	}

	if (!wlr_surface_set_role(surface, &subsurface_role,
			resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE)) {
		free(subsurface);
		return;
	}

	if (wlr_surface_get_root_surface(parent) == surface) {
		wl_resource_post_error(resource,
			WL_SUBCOMPOSITOR_ERROR_BAD_PARENT,
			"wl_subsurface@%" PRIu32
			" cannot be a parent of itself or its ancestor", id);
		free(subsurface);
		return;
	}

	subsurface->synchronized = true;
	subsurface->surface = surface;
	subsurface->resource = wl_resource_create(client, &wl_subsurface_interface,
		wl_resource_get_version(resource), id);
	if (subsurface->resource == NULL) {
		free(subsurface);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(subsurface->resource,
		&subsurface_implementation, subsurface, NULL);

	wlr_surface_set_role_object(surface, subsurface->resource);

	wl_signal_init(&subsurface->events.destroy);

	wl_signal_add(&surface->events.client_commit,
		&subsurface->surface_client_commit);
	subsurface->surface_client_commit.notify =
		subsurface_handle_surface_client_commit;

	// link parent
	subsurface->parent = parent;
	wl_signal_add(&parent->events.destroy, &subsurface->parent_destroy);
	subsurface->parent_destroy.notify = subsurface_handle_parent_destroy;

	wl_list_init(&subsurface->current.link);
	wl_list_insert(parent->pending.subsurfaces_above.prev,
		&subsurface->pending.link);
}

static const struct wl_subcompositor_interface subcompositor_impl = {
	.destroy = subcompositor_handle_destroy,
	.get_subsurface = subcompositor_handle_get_subsurface,
};

static void subcompositor_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_subcompositor *subcompositor = data;
	struct wl_resource *resource =
		wl_resource_create(client, &wl_subcompositor_interface, 1, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &subcompositor_impl,
		subcompositor, NULL);
}

static void subcompositor_handle_display_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_subcompositor *subcompositor =
		wl_container_of(listener, subcompositor, display_destroy);
	wl_signal_emit_mutable(&subcompositor->events.destroy, NULL);
	wl_list_remove(&subcompositor->display_destroy.link);
	wl_global_destroy(subcompositor->global);
	free(subcompositor);
}

struct wlr_subcompositor *wlr_subcompositor_create(struct wl_display *display) {
	struct wlr_subcompositor *subcompositor =
		calloc(1, sizeof(*subcompositor));
	if (!subcompositor) {
		return NULL;
	}

	subcompositor->global = wl_global_create(display,
		&wl_subcompositor_interface, SUBCOMPOSITOR_VERSION,
		subcompositor, subcompositor_bind);
	if (!subcompositor->global) {
		free(subcompositor);
		return NULL;
	}

	wl_signal_init(&subcompositor->events.destroy);

	subcompositor->display_destroy.notify = subcompositor_handle_display_destroy;
	wl_display_add_destroy_listener(display, &subcompositor->display_destroy);

	return subcompositor;
}

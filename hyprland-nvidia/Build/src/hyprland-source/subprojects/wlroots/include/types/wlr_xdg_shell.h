#ifndef TYPES_WLR_XDG_SHELL_H
#define TYPES_WLR_XDG_SHELL_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "xdg-shell-protocol.h"

extern const struct wlr_surface_role xdg_toplevel_surface_role;
extern const struct wlr_surface_role xdg_popup_surface_role;

struct wlr_xdg_surface *create_xdg_surface(
	struct wlr_xdg_client *client, struct wlr_surface *wlr_surface,
	uint32_t id);
void destroy_xdg_surface(struct wlr_xdg_surface *surface);
void destroy_xdg_surface_role_object(struct wlr_xdg_surface *surface);
void xdg_surface_role_commit(struct wlr_surface *wlr_surface);
void xdg_surface_role_unmap(struct wlr_surface *wlr_surface);
void xdg_surface_role_destroy(struct wlr_surface *wlr_surface);

void create_xdg_positioner(struct wlr_xdg_client *client, uint32_t id);

void create_xdg_popup(struct wlr_xdg_surface *surface,
	struct wlr_xdg_surface *parent,
	struct wlr_xdg_positioner *positioner, uint32_t id);
void reset_xdg_popup(struct wlr_xdg_popup *popup);
void destroy_xdg_popup(struct wlr_xdg_popup *popup);
void handle_xdg_popup_committed(struct wlr_xdg_popup *popup);
struct wlr_xdg_popup_configure *send_xdg_popup_configure(
	struct wlr_xdg_popup *popup);
void handle_xdg_popup_ack_configure(struct wlr_xdg_popup *popup,
	struct wlr_xdg_popup_configure *configure);

void create_xdg_toplevel(struct wlr_xdg_surface *surface,
	uint32_t id);
void reset_xdg_toplevel(struct wlr_xdg_toplevel *toplevel);
void destroy_xdg_toplevel(struct wlr_xdg_toplevel *toplevel);
void handle_xdg_toplevel_committed(struct wlr_xdg_toplevel *toplevel);
struct wlr_xdg_toplevel_configure *send_xdg_toplevel_configure(
	struct wlr_xdg_toplevel *toplevel);
void handle_xdg_toplevel_ack_configure(struct wlr_xdg_toplevel *toplevel,
	struct wlr_xdg_toplevel_configure *configure);

#endif

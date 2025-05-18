#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "xdg-shell-client-protocol.h"
#include <wayland-client.h>

// https://wayland-book.com/

typedef struct
{
    struct wl_display    *dpy;
    struct wl_registry   *reg;
    struct wl_compositor *comp;
    struct wl_surface    *surface;
    struct wl_shm        *shm;
    struct wl_seat       *seat;
    struct wl_pointer    *pointer;
    struct xdg_wm_base   *xdg_wm_base;
    struct xdg_surface   *xdg_surface;
    struct xdg_toplevel  *xdg_toplevel;
} State;

static void xdg_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    (void) data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

void registry_global(
    void *data,
    struct wl_registry *wl_registry,
    uint32_t name,
    const char *interface,
    uint32_t version
)
{
    (void) version;
    State *state = data;

    if (!strcmp(interface, wl_shm_interface.name)) {
        state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    }

    else if (!strcmp(interface, wl_compositor_interface.name)) {
        state->comp = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    }

    else if (!strcmp(interface, wl_seat_interface.name)) {
        state->seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 4);
    }

    else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        state->xdg_wm_base = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);

        static struct xdg_wm_base_listener xdg_wm_base_listener = {
            .ping = xdg_base_ping,
        };

        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    }

}

static int create_shm(void)
{

    const char *shm_name = "wl_shm";
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        perror("Failed to create shm file");
        exit(1);
    }
    shm_unlink(shm_name);

    return fd;
}

static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    (void) data;
    wl_buffer_destroy(wl_buffer);
}

struct wl_buffer *draw(State *state)
{

    int fd = create_shm();
    assert(fd >= 0);

    int width = 1920, height = 1080;
    int stride = width * 4; // 4 bytes per pixel
    int size = height * stride;

    int ret = ftruncate(fd, size);
    assert(ret == 0);

    uint32_t *pool_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(pool_data != MAP_FAILED);

    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    int radius = 150;
    int cx = width  / 2;
    int cy = height / 2;

    // draw
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {

            int dx = cx - x;
            int dy = cy - y;
            int dist_sq = dy*dy + dx*dx;

            pool_data[y * width + x] = dist_sq < radius*radius
                ? 0xffff0000
                : 0xffffffff;
        }
    }
    munmap(pool_data, size);

    static struct wl_buffer_listener wl_buffer_listener = {
        .release = wl_buffer_release,
    };

    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);

    return buffer;
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    State *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);

    struct wl_buffer *buffer = draw(state);
    wl_surface_attach(state->surface, buffer, 0, 0);
    wl_surface_commit(state->surface);
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    // State *state = data;
    printf("x: %d\n", surface_x);
    printf("y: %d\n", surface_y);
}

static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities)
{
    printf("%d\n", capabilities);
}

static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name) {}

int main(void)
{

    State state;

    state.dpy = wl_display_connect(NULL);
    assert(state.dpy != NULL);

    state.reg = wl_display_get_registry(state.dpy);
    assert(state.reg != NULL);

    struct wl_registry_listener reg_listener = {
        .global = registry_global,
    };
    wl_registry_add_listener(state.reg, &reg_listener, &state);
    wl_display_roundtrip(state.dpy);


    static struct wl_seat_listener wl_seat_listener = {
        .capabilities = wl_seat_capabilities,
        .name = wl_seat_name,
    };
    wl_seat_add_listener(state.seat, &wl_seat_listener, &state);


    // static struct wl_pointer_listener wl_pointer_listener = {
    //     .motion = wl_pointer_motion,
    // };
    //
    // state.pointer = wl_seat_get_pointer(state.seat);
    // wl_pointer_add_listener(state.pointer, &wl_pointer_listener, &state);



    state.surface     = wl_compositor_create_surface(state.comp);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);

    static struct xdg_surface_listener xdg_surface_listener = {
        .configure = xdg_surface_configure,
    };

    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);

    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "Example client");
    wl_surface_commit(state.surface);

    while (wl_display_dispatch(state.dpy)) {
    }

    return 0;
}

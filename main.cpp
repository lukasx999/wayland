#include <print>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "xdg-shell-client-protocol.h"
#include <wayland-client.h>

// https://wayland-book.com/

struct State
{
    struct wl_display    *dpy          = nullptr;
    struct wl_registry   *reg          = nullptr;
    struct wl_compositor *comp         = nullptr;
    struct wl_surface    *surface      = nullptr;
    struct wl_shm        *shm          = nullptr;
    struct xdg_wm_base   *xdg_wm_base  = nullptr;
    struct xdg_surface   *xdg_surface  = nullptr;
    struct xdg_toplevel  *xdg_toplevel = nullptr;
};



void reg_global(
    void *data,
    struct wl_registry *wl_registry,
    uint32_t name,
    const char *interface,
    uint32_t version
)
{
    (void) version;
    auto state = static_cast<State*>(data);

    if (!strcmp(interface, wl_shm_interface.name)) {
        state->shm = static_cast<struct wl_shm*>(wl_registry_bind(
            wl_registry,
            name,
            &wl_shm_interface,
            1
        ));
    }

    else if (!strcmp(interface, wl_compositor_interface.name)) {
        state->comp = static_cast<struct wl_compositor*>(wl_registry_bind(
            wl_registry,
            name,
            &wl_compositor_interface,
            4
        ));
    }

    else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        state->xdg_wm_base = static_cast<struct xdg_wm_base*>(wl_registry_bind(
            wl_registry,
            name,
            &xdg_wm_base_interface,
            1
        ));

        struct xdg_wm_base_listener xdg_wm_base_listener {
            [](void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
            {
                (void) data;
                xdg_wm_base_pong(xdg_wm_base, serial);
            }
        };

        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    }

}

static int create_shm()
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

struct wl_buffer *draw(State *state)
{

    int fd = create_shm();
    assert(fd >= 0);

    int width = 1920, height = 1080;
    int stride = width * 4; // 4 bytes per pixel
    int size = height * stride;

    int ret = ftruncate(fd, size);
    assert(ret == 0);

    auto pool_data = static_cast<uint32_t*>(mmap(
        NULL,
        size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    ));
    assert(pool_data != MAP_FAILED);

    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    // TODO: draw
    munmap(pool_data, size);

    struct wl_buffer_listener wl_buffer_listener {
        [](void *data, struct wl_buffer *wl_buffer)
        {
            (void) data;
            wl_buffer_destroy(wl_buffer);
        }
    };

    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
    return buffer;
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    auto state = static_cast<State*>(data);
    xdg_surface_ack_configure(xdg_surface, serial);

    auto buffer = draw(state);
    wl_surface_attach(state->surface, buffer, 0, 0);
    wl_surface_commit(state->surface);
}


int main()
{

    State state;

    state.dpy = wl_display_connect(NULL);
    assert(state.dpy != NULL);

    state.reg = wl_display_get_registry(state.dpy);
    assert(state.reg != NULL);

    struct wl_registry_listener reg_listener { reg_global, NULL };
    wl_registry_add_listener(state.reg, &reg_listener, &state);
    wl_display_roundtrip(state.dpy);

    state.surface = wl_compositor_create_surface(state.comp);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);

    struct xdg_surface_listener xdg_surface_listener { xdg_surface_configure };
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);

    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "Example client");
    wl_surface_commit(state.surface);

    while (wl_display_dispatch(state.dpy)) {
    }

    return 0;
}

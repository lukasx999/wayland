#include <print>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>


class State
{
public:
    struct wl_compositor *comp = nullptr;
    struct wl_shm *shm = nullptr;
    State()
    {}
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
        state->shm = static_cast<struct wl_shm*>(wl_registry_bind(wl_registry, name, &wl_shm_interface, 4));
    }

    if (!strcmp(interface, wl_compositor_interface.name)) {
        state->comp = static_cast<struct wl_compositor*>(wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4));
    }



}


int main()
{

    State state;

    struct wl_display *dpy = wl_display_connect(NULL);
    assert(dpy != NULL);

    struct wl_registry *reg = wl_display_get_registry(dpy);
    assert(reg != NULL);

    struct wl_registry_listener reg_listener = {
        .global = reg_global,
        .global_remove = NULL,
    };

    wl_registry_add_listener(reg, &reg_listener, &state);
    wl_display_roundtrip(dpy);

    struct wl_surface *surface = wl_compositor_create_surface(state.comp);


    const char *shm_name = "wl_shm";
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        perror("Failed to create shm file");
        exit(1);
    }
    shm_unlink(shm_name);


    int width = 1920, height = 1080;
    int stride = width * 4; // 4 bytes per pixel
    int shm_pool_size = height * stride * 2; // need 2 buffers for double buffering

    int ret = ftruncate(fd, shm_pool_size);
    assert(ret == 0);


    auto pool_data = static_cast<uint8_t*>(mmap(NULL, shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    assert(pool_data != NULL);

    struct wl_shm_pool *pool = wl_shm_create_pool(state.shm, fd, shm_pool_size);

    int index = 0;
    int offset = height * stride * index;
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, offset, width, height, stride, WL_SHM_FORMAT_XRGB8888);

    uint32_t *pixels = reinterpret_cast<uint32_t*>(&pool_data[offset]);
    memset(pixels, 0, width * height * 4);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, UINT32_MAX, UINT32_MAX);
    wl_surface_commit(surface);


    // while (wl_display_dispatch(dpy) != -1) {
    // }

    wl_registry_destroy(reg);
    wl_display_disconnect(dpy);

    return 0;
}

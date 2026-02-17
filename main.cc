#include <functional>
#include <print>
#include <cassert>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "xdg-shell.h"
#include <xkbcommon/xkbcommon.h>

#include "string_switch.h"

namespace {

struct State {
    struct wl_display* display = nullptr;
    struct wl_surface* surface = nullptr;
    struct wl_registry* registry = nullptr;
    struct wl_compositor* compositor = nullptr;
    struct wl_shm* shm = nullptr;
    struct wl_seat* seat = nullptr;
    struct wl_keyboard* keyboard = nullptr;
    struct xdg_wm_base* xdg_wm_base = nullptr;
    struct xdg_surface* xdg_surface = nullptr;
    struct xdg_toplevel* xdg_toplevel = nullptr;

    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
};


[[nodiscard]] int create_shm_file(size_t size) {

    auto shm_path = "/wayland_shm";
    int fd = shm_open(shm_path, O_RDWR | O_CREAT, 0600);
    assert(fd != -1);
    shm_unlink(shm_path);

    ftruncate(fd, size);

    return fd;
}

struct wl_buffer_listener wl_buffer_listener_ {
    .release = [](void* data, struct wl_buffer* wl_buffer) {
        wl_buffer_destroy(wl_buffer);
    }
};

[[nodiscard]] struct wl_buffer* draw_frame(const State& state) {

    int width = 1920;
    int height = 1080;
    int stride = 4;
    size_t pool_size = width * height * stride;

    int fd = create_shm_file(pool_size);

    uint32_t* pool_data = static_cast<uint32_t*>(mmap(nullptr, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    assert(pool_data != MAP_FAILED);

    struct wl_shm_pool* pool = wl_shm_create_pool(state.shm, fd, pool_size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride*width, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            pool_data[x + y * width] = 0x0;
        }
    }

    for (int x = 0; x < 500; ++x) {
        for (int y = 0; y < 500; ++y) {
            pool_data[x + y * width] = 0xffffffff;
        }
    }

    float radius = 100.0f;
    float center_x = width / 2.0f;
    float center_y = height / 2.0f;

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            float diff_x = center_x - x;
            float diff_y = center_y - y;

            float len = std::sqrt(diff_x*diff_x + diff_y*diff_y);
            if (len <= radius) {
                pool_data[x + y * width] = 0xffffffff;
            }

        }
    }

    munmap(pool_data, pool_size);

    wl_buffer_add_listener(buffer, &wl_buffer_listener_, nullptr);
    return buffer;
}

void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);

    State& state = *static_cast<State*>(data);

    auto buffer = draw_frame(state);

    wl_surface_attach(state.surface, buffer, 0, 0);
    wl_surface_damage_buffer(state.surface, 0, 0, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
    wl_surface_commit(state.surface);
}

void registry_handle_global(void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version) {
    State* state = static_cast<State*>(data);

    StringSwitch<std::function<void()>>(interface)
        .case_(wl_compositor_interface.name, [&] {
            state->compositor = static_cast<struct wl_compositor*>(wl_registry_bind(wl_registry, name, &wl_compositor_interface, version));
        })

        .case_(wl_shm_interface.name, [&] {
            state->shm = static_cast<struct wl_shm*>(wl_registry_bind(wl_registry, name, &wl_shm_interface, version));
        })

        .case_(xdg_wm_base_interface.name, [&] {
            state->xdg_wm_base = static_cast<struct xdg_wm_base*>(wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, version));
        })

        .case_(wl_seat_interface.name, [&] {
            state->seat = static_cast<struct wl_seat*>(wl_registry_bind(wl_registry, name, &wl_seat_interface, version));
        })

        .default_([] {})
        .done()();
}

struct xdg_wm_base_listener xdg_wm_base_listener_ {
    .ping = [](void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
        xdg_wm_base_pong(xdg_wm_base, serial);
    }
};

struct xdg_surface_listener xdg_surface_listener_ {
    .configure = xdg_surface_configure,
};

struct wl_registry_listener registry_listener_ {
    .global = registry_handle_global,
    .global_remove = nullptr,
};

extern struct wl_callback_listener frame_callback_listener;

void frame_callback(void* data, struct wl_callback* wl_callback, uint32_t callback_data) {
    State& state = *static_cast<State*>(data);

    wl_callback_destroy(wl_callback);

    struct wl_callback* frame_callback = wl_surface_frame(state.surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, &state);

    struct wl_buffer* buffer = draw_frame(state);

    wl_surface_attach(state.surface, buffer, 0, 0);
    wl_surface_damage_buffer(state.surface, 0, 0, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
    wl_surface_commit(state.surface);
}

struct wl_callback_listener frame_callback_listener {
    .done = frame_callback,
};

void on_key_press(void* data, struct wl_keyboard* wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
}

struct wl_keyboard_listener keyboard_listener {
    .keymap = [](void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) { },
    .enter = [](void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) { },
    .leave = [](void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) { },
    .key = on_key_press,
    .modifiers = [](void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) { },
    .repeat_info = [](void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) { },
};

void init_egl(State& state) {

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    state.egl_display = eglGetDisplay(state.display);
    assert(state.egl_display != EGL_NO_DISPLAY);

    EGLint major, minor;
    assert(eglInitialize(state.egl_display, &major, &minor) == EGL_TRUE);

    EGLint config_count;
    eglGetConfigs(state.egl_display, nullptr, 0, &config_count);

    std::vector<EGLConfig> configs(config_count);

    EGLint n;
    eglChooseConfig(state.egl_display, config_attribs, configs.data(), config_count, &n);

    state.egl_context = eglCreateContext(state.egl_display, configs[0], EGL_NO_CONTEXT, context_attribs);
}

} // namespace

int main() {

    State state;

    state.display = wl_display_connect(nullptr);

    init_egl(state);
    wl_egl_window* egl_window = wl_egl_window_create(state.surface, 1920, 1080);
    assert(egl_window != EGL_NO_SURFACE);

    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener_, &state);
    wl_display_roundtrip(state.display);

    state.keyboard = wl_seat_get_keyboard(state.seat);
    wl_keyboard_add_listener(state.keyboard, &keyboard_listener, &state);

    state.surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "my app");

    wl_surface_commit(state.surface);

    xdg_wm_base_add_listener(state.xdg_wm_base, &xdg_wm_base_listener_, nullptr);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener_, &state);

    struct wl_callback* frame_callback = wl_surface_frame(state.surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, &state);


    while (wl_display_dispatch(state.display) != -1);

    wl_display_disconnect(state.display);
}

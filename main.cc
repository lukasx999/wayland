#include <functional>
#include <print>
#include <cassert>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "xdg-shell.h"
#include <xkbcommon/xkbcommon.h>

// #define GLAD_GL_IMPLEMENTATION
// #include <glad/gl.h>
#include <GL/gl.h>

#include "string_switch.h"

namespace {

struct State {
    struct wl_display* wl_display = nullptr;
    struct wl_surface* wl_surface = nullptr;
    struct wl_registry* wl_registry = nullptr;
    struct wl_compositor* wl_compositor = nullptr;
    struct wl_seat* wl_seat = nullptr;
    struct wl_keyboard* wl_keyboard = nullptr;

    struct xdg_wm_base* xdg_wm_base = nullptr;
    struct xdg_surface* xdg_surface = nullptr;
    struct xdg_toplevel* xdg_toplevel = nullptr;

    EGLDisplay egl_display = nullptr;
    EGLSurface egl_surface = nullptr;
    EGLContext egl_context = nullptr;
    EGLConfig egl_config = nullptr;
};

void draw_egl(const State& state) {
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(state.egl_display, state.egl_surface);
}

void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);

    State& state = *static_cast<State*>(data);
    eglSwapBuffers(state.egl_display, state.egl_surface);
}

void registry_handle_global(void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version) {
    State* state = static_cast<State*>(data);

    StringSwitch<std::function<void()>>(interface)
        .case_(wl_compositor_interface.name, [&] {
            state->wl_compositor = static_cast<struct wl_compositor*>(wl_registry_bind(wl_registry, name, &wl_compositor_interface, version));
        })

        .case_(xdg_wm_base_interface.name, [&] {
            state->xdg_wm_base = static_cast<struct xdg_wm_base*>(wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, version));
        })

        .case_(wl_seat_interface.name, [&] {
            state->wl_seat = static_cast<struct wl_seat*>(wl_registry_bind(wl_registry, name, &wl_seat_interface, version));
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

    struct wl_callback* frame_callback = wl_surface_frame(state.wl_surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, &state);

    draw_egl(state);
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

    state.egl_display = eglGetDisplay(state.wl_display);
    assert(state.egl_display != EGL_NO_DISPLAY);

    EGLint major, minor;
    assert(eglInitialize(state.egl_display, &major, &minor) == EGL_TRUE);

    EGLint config_count;
    eglGetConfigs(state.egl_display, nullptr, 0, &config_count);

    std::vector<EGLConfig> configs(config_count);

    EGLint n;
    eglChooseConfig(state.egl_display, config_attribs, configs.data(), config_count, &n);

    state.egl_config = configs[0];
    state.egl_context = eglCreateContext(state.egl_display, state.egl_config, EGL_NO_CONTEXT, context_attribs);

    struct wl_egl_window* egl_window = wl_egl_window_create(state.wl_surface, 1920, 1080);
    assert(egl_window != EGL_NO_SURFACE);

    state.egl_surface = eglCreateWindowSurface(state.egl_display, state.egl_config, egl_window, nullptr);
    assert(eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface, state.egl_context));
}

} // namespace

int main() {

    State state;

    state.wl_display = wl_display_connect(nullptr);

    state.wl_registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(state.wl_registry, &registry_listener_, &state);
    wl_display_roundtrip(state.wl_display);


    state.wl_keyboard = wl_seat_get_keyboard(state.wl_seat);
    wl_keyboard_add_listener(state.wl_keyboard, &keyboard_listener, &state);

    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    init_egl(state);

    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "my app");

    xdg_wm_base_add_listener(state.xdg_wm_base, &xdg_wm_base_listener_, nullptr);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener_, &state);

    struct wl_callback* frame_callback = wl_surface_frame(state.wl_surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, &state);

    eglSwapBuffers(state.egl_display, state.egl_surface);

    while (wl_display_dispatch(state.wl_display) != -1);

    wl_display_disconnect(state.wl_display);
}

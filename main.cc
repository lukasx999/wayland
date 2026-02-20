#include <functional>
#include <print>
#include <cassert>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "xdg-shell.h"
#include <xkbcommon/xkbcommon.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <gfx/gfx.h>

#include "util.h"

namespace {

struct State {
    struct wl_display*    wl_display    = nullptr;
    struct wl_surface*    wl_surface    = nullptr;
    struct wl_registry*   wl_registry   = nullptr;
    struct wl_compositor* wl_compositor = nullptr;
    struct wl_seat*       wl_seat       = nullptr;
    struct wl_keyboard*   wl_keyboard   = nullptr;

    struct xdg_wm_base*  xdg_wm_base  = nullptr;
    struct xdg_surface*  xdg_surface  = nullptr;
    struct xdg_toplevel* xdg_toplevel = nullptr;

    struct wl_egl_window* egl_window = nullptr;

    EGLDisplay egl_display = nullptr;
    EGLSurface egl_surface = nullptr;
    EGLContext egl_context = nullptr;
    EGLConfig  egl_config  = nullptr;

    std::optional<gfx::ExternalContext> ctx;
};

void draw(gfx::Renderer& rd) {
    rd.clear_background(gfx::Color::blue());
    rd.draw_rectangle(0, 0, 300, 300, gfx::Color::orange().set_alpha(128));
    rd.draw_circle(rd.get_surface().get_center(), 150, gfx::Color::red());
    rd.draw_triangle(0, 0, 100, 100, 0, 100, gfx::Color::red());
}

void xdg_surface_configure([[maybe_unused]] void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}

void registry_handle_global(void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version) {
    State& state = *static_cast<State*>(data);

    using namespace std::placeholders;
    auto bind_global = std::bind(wl_registry_bind, wl_registry, name, _1, version);

    util::StringSwitch<std::function<void()>>(interface)
        .case_(wl_compositor_interface.name, [&] {
            state.wl_compositor = static_cast<struct wl_compositor*>(bind_global(&wl_compositor_interface));
        })

        .case_(xdg_wm_base_interface.name, [&] {
            state.xdg_wm_base = static_cast<struct xdg_wm_base*>(bind_global(&xdg_wm_base_interface));
        })

        .case_(wl_seat_interface.name, [&] {
            state.wl_seat = static_cast<struct wl_seat*>(bind_global(&wl_seat_interface));
        })

        .default_([] { })
        .done()();
}

struct xdg_wm_base_listener xdg_wm_base_listener_ {
    .ping = []([[maybe_unused]] void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
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

void frame_callback(void* data, struct wl_callback* wl_callback, [[maybe_unused]] uint32_t callback_data) {
    State& state = *static_cast<State*>(data);

    wl_callback_destroy(wl_callback);

    struct wl_callback* frame_callback = wl_surface_frame(state.wl_surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, &state);

    state.ctx->draw(draw);

    eglSwapBuffers(state.egl_display, state.egl_surface);
}

struct wl_callback_listener frame_callback_listener {
    .done = frame_callback,
};

void xdg_toplevel_configure(void* data, [[maybe_unused]] struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height, [[maybe_unused]] struct wl_array* states) {
    State& state = *static_cast<State*>(data);
    glViewport(0, 0, width, height);
    wl_egl_window_resize(state.egl_window, width, height, 0, 0);
}

struct xdg_toplevel_listener toplevel_listener {
    .configure = xdg_toplevel_configure,
    .close = util::DefaultConstructedFunction<decltype(xdg_toplevel_listener::close)>::value,
    .configure_bounds = util::DefaultConstructedFunction<decltype(xdg_toplevel_listener::configure_bounds)>::value,
    .wm_capabilities = util::DefaultConstructedFunction<decltype(xdg_toplevel_listener::wm_capabilities)>::value,
};

struct wl_keyboard_listener keyboard_listener {
    .keymap = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::keymap)>::value,
    .enter = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::enter)>::value,
    .leave = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::leave)>::value,
    .key = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::key)>::value,
    .modifiers = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::modifiers)>::value,
    .repeat_info = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::repeat_info)>::value,
};

void init_egl(State& state, int width, int height) {

    std::array config_attribs {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    std::array context_attribs {
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 5,
        EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
        EGL_NONE
    };

    state.egl_display = eglGetDisplay(state.wl_display);
    assert(state.egl_display != EGL_NO_DISPLAY);

    EGLint major, minor;
    assert(eglInitialize(state.egl_display, &major, &minor) == EGL_TRUE);

    EGLint config_count;
    eglGetConfigs(state.egl_display, nullptr, 0, &config_count);

    std::vector<EGLConfig> configs(config_count);

    assert(eglBindAPI(EGL_OPENGL_API));

    EGLint n;
    eglChooseConfig(state.egl_display, config_attribs.data(), configs.data(), config_count, &n);

    state.egl_config = configs.front();
    state.egl_context = eglCreateContext(state.egl_display, state.egl_config, EGL_NO_CONTEXT, context_attribs.data());
    assert(state.egl_context != EGL_NO_CONTEXT);

    state.egl_window = wl_egl_window_create(state.wl_surface, width, height);
    assert(state.egl_window != EGL_NO_SURFACE);

    state.egl_surface = eglCreateWindowSurface(state.egl_display, state.egl_config, state.egl_window, nullptr);
    assert(eglMakeCurrent(state.egl_display, state.egl_surface, state.egl_surface, state.egl_context));
}

} // namespace

int main() {

    const char* window_title = "my wayland app";
    int width = 1920;
    int height = 1080;

    State state;

    state.wl_display = wl_display_connect(nullptr);
    state.wl_registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(state.wl_registry, &registry_listener_, &state);
    wl_display_roundtrip(state.wl_display);

    state.wl_keyboard = wl_seat_get_keyboard(state.wl_seat);
    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);

    xdg_toplevel_set_title(state.xdg_toplevel, window_title);

    wl_keyboard_add_listener(state.wl_keyboard, &keyboard_listener, &state);
    xdg_toplevel_add_listener(state.xdg_toplevel, &toplevel_listener, &state);
    xdg_wm_base_add_listener(state.xdg_wm_base, &xdg_wm_base_listener_, nullptr);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener_, nullptr);

    init_egl(state, width, height);

    struct wl_callback* frame_callback = wl_surface_frame(state.wl_surface);
    wl_callback_add_listener(frame_callback, &frame_callback_listener, &state);

    eglSwapBuffers(state.egl_display, state.egl_surface);

    auto get_width = [&] {
        int width;
        wl_egl_window_get_attached_size(state.egl_window, &width, nullptr);
        return width;
    };

    auto get_height = [&] {
        int height;
        wl_egl_window_get_attached_size(state.egl_window, nullptr, &height);
        return height;
    };

    state.ctx.emplace(get_width, get_height);

    while (wl_display_dispatch(state.wl_display) != -1);

    wl_display_disconnect(state.wl_display);
}

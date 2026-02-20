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

class WaylandWindow {

    using DrawFn = std::function<void(gfx::Renderer&)>;
    DrawFn m_draw_fn;

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

    std::optional<gfx::ExternalContext> m_gfx_context;

public:
    WaylandWindow(int width, int height, const char* title) {

        wl_display = wl_display_connect(nullptr);
        wl_registry = wl_display_get_registry(wl_display);
        wl_registry_add_listener(wl_registry, &m_registry_listene, this);
        wl_display_roundtrip(wl_display);

        wl_keyboard = wl_seat_get_keyboard(wl_seat);
        wl_surface = wl_compositor_create_surface(wl_compositor);
        xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, wl_surface);
        xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

        xdg_toplevel_set_title(xdg_toplevel, title);

        wl_keyboard_add_listener(wl_keyboard, &m_keyboard_listener, this);
        xdg_toplevel_add_listener(xdg_toplevel, &m_xdg_toplevel_listener, this);
        xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener_, nullptr);
        xdg_surface_add_listener(xdg_surface, &m_xdg_surface_listener, nullptr);

        init_egl(width, height);

        struct wl_callback* frame_callback = wl_surface_frame(wl_surface);
        wl_callback_add_listener(frame_callback, &m_frame_callback_listener, this);

        eglSwapBuffers(egl_display, egl_surface);

        auto get_width = [&] {
            int width;
            wl_egl_window_get_attached_size(egl_window, &width, nullptr);
            return width;
        };

        auto get_height = [&] {
            int height;
            wl_egl_window_get_attached_size(egl_window, nullptr, &height);
            return height;
        };

        m_gfx_context.emplace(get_width, get_height);
    }

    ~WaylandWindow() {
        wl_display_disconnect(wl_display);
    }

    void draw_loop(DrawFn draw_fn) {
        m_draw_fn = draw_fn;
        while (wl_display_dispatch(wl_display) != -1);
    }

private:
    static void xdg_surface_configure([[maybe_unused]] void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
        xdg_surface_ack_configure(xdg_surface, serial);
    }

    static void registry_handle_global(void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version) {
        WaylandWindow& self = *static_cast<WaylandWindow*>(data);

        using namespace std::placeholders;
        auto bind_global = std::bind(wl_registry_bind, wl_registry, name, _1, version);

        util::StringSwitch<std::function<void()>>(interface)
            .case_(wl_compositor_interface.name, [&] {
                self.wl_compositor = static_cast<struct wl_compositor*>(bind_global(&wl_compositor_interface));
            })

            .case_(xdg_wm_base_interface.name, [&] {
                self.xdg_wm_base = static_cast<struct xdg_wm_base*>(bind_global(&xdg_wm_base_interface));
            })

            .case_(wl_seat_interface.name, [&] {
                self.wl_seat = static_cast<struct wl_seat*>(bind_global(&wl_seat_interface));
            })

            .default_([] { })
            .done()();
    }

    struct xdg_wm_base_listener xdg_wm_base_listener_ {
        .ping = []([[maybe_unused]] void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
            xdg_wm_base_pong(xdg_wm_base, serial);
        }
    };

    static void frame_callback(void* data, struct wl_callback* wl_callback, [[maybe_unused]] uint32_t callback_data) {
        WaylandWindow& self = *static_cast<WaylandWindow*>(data);

        wl_callback_destroy(wl_callback);

        struct wl_callback* frame_callback = wl_surface_frame(self.wl_surface);
        wl_callback_add_listener(frame_callback, &m_frame_callback_listener, &self);

        self.m_gfx_context->draw(self.m_draw_fn);

        eglSwapBuffers(self.egl_display, self.egl_surface);
    }

    static void xdg_toplevel_configure(void* data, [[maybe_unused]] struct xdg_toplevel* xdg_toplevel,
                                       int32_t width, int32_t height, [[maybe_unused]] struct wl_array* states) {
        WaylandWindow& self = *static_cast<WaylandWindow*>(data);
        glViewport(0, 0, width, height);
        wl_egl_window_resize(self.egl_window, width, height, 0, 0);
    }

    void init_egl(int width, int height) {

        std::array config_attribs {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE,   8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8,
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

        egl_display = eglGetDisplay(wl_display);
        assert(egl_display != EGL_NO_DISPLAY);

        EGLint major, minor;
        assert(eglInitialize(egl_display, &major, &minor) == EGL_TRUE);

        EGLint config_count;
        eglGetConfigs(egl_display, nullptr, 0, &config_count);

        std::vector<EGLConfig> configs(config_count);

        assert(eglBindAPI(EGL_OPENGL_API));

        EGLint n;
        eglChooseConfig(egl_display, config_attribs.data(), configs.data(), config_count, &n);

        egl_config = configs.front();
        egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs.data());
        assert(egl_context != EGL_NO_CONTEXT);

        egl_window = wl_egl_window_create(wl_surface, width, height);
        assert(egl_window != EGL_NO_SURFACE);

        egl_surface = eglCreateWindowSurface(egl_display, egl_config, egl_window, nullptr);
        assert(eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context));
    }

    static inline xdg_surface_listener m_xdg_surface_listener {
        .configure = xdg_surface_configure,
    };

    static inline wl_registry_listener m_registry_listene {
        .global = registry_handle_global,
        .global_remove = util::DefaultConstructedFunction<decltype(wl_registry_listener::global_remove)>::value,
    };

    static inline wl_callback_listener m_frame_callback_listener {
        .done = frame_callback,
    };

    static inline xdg_toplevel_listener m_xdg_toplevel_listener {
        .configure        = xdg_toplevel_configure,
        .close            = util::DefaultConstructedFunction<decltype(xdg_toplevel_listener::close)>::value,
        .configure_bounds = util::DefaultConstructedFunction<decltype(xdg_toplevel_listener::configure_bounds)>::value,
        .wm_capabilities  = util::DefaultConstructedFunction<decltype(xdg_toplevel_listener::wm_capabilities)>::value,
    };

    static inline wl_keyboard_listener m_keyboard_listener {
        .keymap      = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::keymap)>::value,
        .enter       = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::enter)>::value,
        .leave       = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::leave)>::value,
        .key         = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::key)>::value,
        .modifiers   = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::modifiers)>::value,
        .repeat_info = util::DefaultConstructedFunction<decltype(wl_keyboard_listener::repeat_info)>::value,
    };

};


} // namespace

int main() {

    WaylandWindow window(1920, 1080, "my wayland app");
    window.draw_loop([&](gfx::Renderer& rd) {

        static gfx::Animation<gfx::Vec> anim { { 0, 0 }, rd.get_surface().get_center(), std::chrono::seconds(2), gfx::interpolators::ease_in_cubic};
        static bool first = true;
        if (first) {
            anim.start();
            first = false;
        }

        rd.clear_background(gfx::Color::blue());
        rd.draw_rectangle(0, 0, 300, 300, gfx::Color::orange());
        rd.draw_circle(rd.get_surface().get_center(), 150, gfx::Color::red());
        rd.draw_circle(anim, 150, gfx::Color::lightblue());
        rd.draw_triangle(0, 0, 100, 100, 0, 100, gfx::Color::red());
    });

}

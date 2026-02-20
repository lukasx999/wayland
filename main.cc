#include <functional>
#include <print>
#include <cassert>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell.h"
#include "wlr-layer-shell-unstable-v1.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <gfx/gfx.h>

#include "util.h"

namespace {

class WaylandWindow : public gfx::Surface {

    using DrawFn = std::function<void(gfx::Renderer&)>;
    DrawFn m_draw_fn;

    struct wl_display*    m_wl_display    = nullptr;
    struct wl_surface*    m_wl_surface    = nullptr;
    struct wl_registry*   m_wl_registry   = nullptr;
    struct wl_compositor* m_wl_compositor = nullptr;
    struct wl_seat*       m_wl_seat       = nullptr;
    struct wl_keyboard*   m_wl_keyboard   = nullptr;

    struct xdg_wm_base*  m_xdg_wm_base  = nullptr;
    struct xdg_surface*  m_xdg_surface  = nullptr;
    struct xdg_toplevel* m_xdg_toplevel = nullptr;

    struct zwlr_layer_shell_v1* m_zwlr_layer_shell = nullptr;
    struct zwlr_layer_surface_v1* m_zwlr_layer_surface = nullptr;

    struct wl_egl_window* m_egl_window = nullptr;
    EGLDisplay m_egl_display = nullptr;
    EGLSurface m_egl_surface = nullptr;
    EGLContext m_egl_context = nullptr;
    EGLConfig  m_egl_config  = nullptr;

    // TODO: initialize gl context in pimpl
    std::optional<gfx::Renderer> m_renderer;

    enum class Type { XDGToplevel, WLRLayerSurface } m_type = Type::WLRLayerSurface;

public:
    WaylandWindow(int width, int height, const char* title) {

        m_wl_display = wl_display_connect(nullptr);
        m_wl_registry = wl_display_get_registry(m_wl_display);
        wl_registry_add_listener(m_wl_registry, &m_registry_listener, this);
        wl_display_roundtrip(m_wl_display);

        m_wl_keyboard = wl_seat_get_keyboard(m_wl_seat);
        m_wl_surface = wl_compositor_create_surface(m_wl_compositor);

        xdg_wm_base_add_listener(m_xdg_wm_base, &m_xdg_wm_base_listener, nullptr);
        wl_keyboard_add_listener(m_wl_keyboard, &m_keyboard_listener, this);

        if (m_type == Type::XDGToplevel) {
            m_xdg_surface = xdg_wm_base_get_xdg_surface(m_xdg_wm_base, m_wl_surface);
            m_xdg_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
            xdg_toplevel_set_title(m_xdg_toplevel, title);

            xdg_toplevel_add_listener(m_xdg_toplevel, &m_xdg_toplevel_listener, this);
            xdg_surface_add_listener(m_xdg_surface, &m_xdg_surface_listener, nullptr);
        }

        if (m_type == Type::WLRLayerSurface) {
            m_zwlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(m_zwlr_layer_shell, m_wl_surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, title);

            zwlr_layer_surface_v1_add_listener(m_zwlr_layer_surface, &m_zwlr_layer_surface_v1_listener, this);
            zwlr_layer_surface_v1_set_size(m_zwlr_layer_surface, 0, 100);
            zwlr_layer_surface_v1_set_anchor(m_zwlr_layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
            zwlr_layer_surface_v1_set_margin(m_zwlr_layer_surface, 10, 10, 10, 10);
        }

        wl_callback* frame_callback = wl_surface_frame(m_wl_surface);
        wl_callback_add_listener(frame_callback, &m_frame_callback_listener, this);

        init_egl(width, height);
        m_renderer.emplace(*this);

        wl_surface_commit(m_wl_surface);
        // doesnt work for some reason
        // eglSwapBuffers(m_egl_display, m_egl_surface);
    }

    ~WaylandWindow() {
        wl_display_disconnect(m_wl_display);
    }

    [[nodiscard]] int get_width() const override {
        int width;
        wl_egl_window_get_attached_size(m_egl_window, &width, nullptr);
        return width;
    };

    [[nodiscard]] int get_height() const override {
        int height;
        wl_egl_window_get_attached_size(m_egl_window, nullptr, &height);
        return height;
    };

    void draw_loop(DrawFn draw_fn) {
        m_draw_fn = draw_fn;
        while (wl_display_dispatch(m_wl_display) != -1);
    }

private:
    static void bind_globals(void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version) {
        WaylandWindow& self = *static_cast<WaylandWindow*>(data);

        using namespace std::placeholders;
        auto bind_global = std::bind(wl_registry_bind, wl_registry, name, _1, version);

        util::StringSwitch<std::function<void()>>(interface)
            .case_(wl_compositor_interface.name, [&] {
                self.m_wl_compositor = static_cast<struct wl_compositor*>(bind_global(&wl_compositor_interface));
            })

            .case_(xdg_wm_base_interface.name, [&] {
                self.m_xdg_wm_base = static_cast<struct xdg_wm_base*>(bind_global(&xdg_wm_base_interface));
            })

            .case_(wl_seat_interface.name, [&] {
                self.m_wl_seat = static_cast<struct wl_seat*>(bind_global(&wl_seat_interface));
            })

            .case_(zwlr_layer_shell_v1_interface.name, [&] {
                self.m_zwlr_layer_shell = static_cast<struct zwlr_layer_shell_v1*>(bind_global(&zwlr_layer_shell_v1_interface));
            })

            .default_([] { })
            .done()();
    }

    static void xdg_surface_configure([[maybe_unused]] void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
        xdg_surface_ack_configure(xdg_surface, serial);
    }

    static void render_frame(void* data, struct wl_callback* wl_callback, [[maybe_unused]] uint32_t callback_data) {
        WaylandWindow& self = *static_cast<WaylandWindow*>(data);

        wl_callback_destroy(wl_callback);

        struct wl_callback* frame_callback = wl_surface_frame(self.m_wl_surface);
        wl_callback_add_listener(frame_callback, &m_frame_callback_listener, &self);

        self.m_draw_fn(*self.m_renderer);
        eglSwapBuffers(self.m_egl_display, self.m_egl_surface);
    }

    static void xdg_toplevel_configure(void* data, [[maybe_unused]] struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height, [[maybe_unused]] struct wl_array* states) {
        WaylandWindow& self = *static_cast<WaylandWindow*>(data);
        glViewport(0, 0, width, height);
        wl_egl_window_resize(self.m_egl_window, width, height, 0, 0);
    }

    static void zwlr_layer_surface_v1_configure(void* data, struct zwlr_layer_surface_v1* zwlr_layer_surface_v1, uint32_t serial, uint32_t width, uint32_t height) {
        zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);
        WaylandWindow& self = *static_cast<WaylandWindow*>(data);
        glViewport(0, 0, width, height);
        wl_egl_window_resize(self.m_egl_window, width, height, 0, 0);
    }

    void init_egl(int width, int height) {
        // TODO: remove asseration in favor of proper error handling

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

        m_egl_display = eglGetDisplay(m_wl_display);
        assert(m_egl_display != EGL_NO_DISPLAY);

        EGLint major, minor;
        assert(eglInitialize(m_egl_display, &major, &minor) == EGL_TRUE);

        EGLint config_count;
        eglGetConfigs(m_egl_display, nullptr, 0, &config_count);

        std::vector<EGLConfig> configs(config_count);

        assert(eglBindAPI(EGL_OPENGL_API));

        EGLint n;
        eglChooseConfig(m_egl_display, config_attribs.data(), configs.data(), config_count, &n);

        m_egl_config = configs.front();
        m_egl_context = eglCreateContext(m_egl_display, m_egl_config, EGL_NO_CONTEXT, context_attribs.data());
        assert(m_egl_context != EGL_NO_CONTEXT);

        m_egl_window = wl_egl_window_create(m_wl_surface, width, height);
        assert(m_egl_window != EGL_NO_SURFACE);

        m_egl_surface = eglCreateWindowSurface(m_egl_display, m_egl_config, m_egl_window, nullptr);
        assert(eglMakeCurrent(m_egl_display, m_egl_surface, m_egl_surface, m_egl_context));
    }

    static inline zwlr_layer_surface_v1_listener m_zwlr_layer_surface_v1_listener {
        .configure = zwlr_layer_surface_v1_configure,
        .closed = util::DefaultConstructedFunction<decltype(zwlr_layer_surface_v1_listener::closed)>::value,
    };

    static inline xdg_surface_listener m_xdg_surface_listener {
        .configure = xdg_surface_configure,
    };

    static inline xdg_wm_base_listener m_xdg_wm_base_listener {
        .ping = []([[maybe_unused]] void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
            xdg_wm_base_pong(xdg_wm_base, serial);
        }
    };

    static inline wl_registry_listener m_registry_listener {
        .global = bind_globals,
        .global_remove = util::DefaultConstructedFunction<decltype(wl_registry_listener::global_remove)>::value,
    };

    static inline wl_callback_listener m_frame_callback_listener {
        .done = render_frame,
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

#include <functional>
#include <print>
#include <cassert>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <wayland-client.h>
#include "xdg-shell.h"

template <typename T>
class StringSwitch {
    const std::string_view m_string;
    std::optional<T> m_value;
    T m_default{};

public:
    constexpr StringSwitch(std::string_view string) : m_string(string) { }

    constexpr StringSwitch& case_(std::string_view query, T value) {
        if (query == m_string)
            m_value = value;

        return *this;
    }

    constexpr StringSwitch& default_(T value) {
        m_default = value;

        return *this;
    }

    constexpr operator T() const {
        return done();
    }

    [[nodiscard]] constexpr T done() const {
        return m_value.value_or(m_default);
    }

};

consteval void test_string_switch() {

    static_assert(StringSwitch<int>("foo")
    .case_("foo", 1)
    .case_("bar", 2)
    .case_("baz", 3)
     == 1);

    static_assert(StringSwitch<int>("foo")
    .case_("bar", 2)
    .case_("baz", 3)
    .default_(1)
     == 1);

    static_assert(StringSwitch<int>("foo")
    .case_("bar", 2)
    .case_("baz", 3)
     == 0);

}

namespace {

struct State {
    struct wl_display* display = nullptr;
    struct wl_surface* surface = nullptr;
    struct wl_registry* registry = nullptr;
    struct wl_compositor* compositor = nullptr;
    struct wl_shm* shm = nullptr;
    struct xdg_wm_base* xdg_wm_base = nullptr;
    struct xdg_surface* xdg_surface = nullptr;
    struct xdg_toplevel* xdg_toplevel = nullptr;
};

[[nodiscard]] int create_shm_file(size_t size) {

    auto shm_path = "my_shm";
    int fd = shm_open(shm_path, O_RDWR | O_CREAT, 0600);
    assert(fd != -1);
    shm_unlink(shm_path);

    ftruncate(fd, size);

    return fd;
}

void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {

    State& state = *static_cast<State*>(data);

    std::println("configure");
    xdg_surface_ack_configure(xdg_surface, serial);

    int width = 1920;
    int height = 1080;
    int stride = 4;
    size_t pool_size = width * height * stride;

    int fd = create_shm_file(pool_size);

    uint32_t* pool_data = static_cast<uint32_t*>(mmap(nullptr, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            pool_data[x + y * width] = 0x0;
        }
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(state.shm, fd, pool_size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

    wl_surface_attach(state.surface, buffer, 0, 0);
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

} // namespace

int main() {

    State state;

    state.display = wl_display_connect(nullptr);
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener_, &state);
    wl_display_roundtrip(state.display);



    state.surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "my app");

    xdg_wm_base_add_listener(state.xdg_wm_base, &xdg_wm_base_listener_, nullptr);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener_, &state);

    wl_surface_commit(state.surface);

    while (wl_display_dispatch(state.display) != -1);

    wl_display_disconnect(state.display);
}

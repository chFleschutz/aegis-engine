module;
#include <expected>
#include <string>
#include <vector>

export module aegis.rhi:context;
import :error;
import aegis.platform.window;

import vulkan_hpp;

export namespace aegis::rhi
{
class Context
{
public:
    struct Desc
    {
        std::string appName;
        const platform::Window& window;
    };

#ifdef NDEBUG
    static constexpr bool enableValidation = false;
#else
    static constexpr bool enableValidation = true;
#endif

    static auto create(const Desc& desc) -> std::expected<Context, Error>;

    [[nodiscard]] auto instance() const -> const vk::raii::Instance& { return m_instance; }
    [[nodiscard]] auto surface() const -> const vk::raii::SurfaceKHR& { return m_surface; }

private:
    Context(
        vk::raii::Context context,
        vk::raii::Instance instance,
        vk::raii::DebugUtilsMessengerEXT messenger,
        vk::raii::SurfaceKHR surface);

    [[nodiscard]] static auto createInstance(const vk::raii::Context& context, const Desc& desc)
        -> std::expected<vk::raii::Instance, Error>;
    [[nodiscard]] static auto createDebugMessenger(const vk::raii::Instance& instance)
        -> std::expected<vk::raii::DebugUtilsMessengerEXT, Error>;
    [[nodiscard]] static auto createSurface(const vk::raii::Instance& instance, const Desc& desc)
        -> std::expected<vk::raii::SurfaceKHR, Error>;

    [[nodiscard]] static auto findExtensions() -> std::vector<const char*>;
    [[nodiscard]] static auto findLayers(const vk::raii::Context& context)
        -> std::expected<std::vector<const char*>, Error>;

    vk::raii::Context m_context;
    vk::raii::Instance m_instance;
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger;
    vk::raii::SurfaceKHR m_surface;
};
}

module;
#include <string>
#include <vector>

export module aegis.rhi:context;
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
        platform::Window& window;
    };

#ifdef NDEBUG
    static constexpr bool enableValidation = false;
#else
    static constexpr bool enableValidation = true;
#endif

    explicit Context(const Desc& desc);

    [[nodiscard]] auto instance() const -> const vk::raii::Instance& { return m_instance; }
    [[nodiscard]] auto surface() const -> const vk::raii::SurfaceKHR& { return m_surface; }

private:
    auto createInstance(const Desc& desc) -> void;
    auto createDebugMessenger() -> void;
    auto createSurface(const Desc& desc) -> void;

    [[nodiscard]] static auto findExtensions() -> std::vector<const char*>;
    [[nodiscard]] auto findLayers() const -> std::vector<const char*>;

    vk::raii::Context m_context;
    vk::raii::Instance m_instance{ nullptr };
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger{ nullptr };
    vk::raii::SurfaceKHR m_surface{ nullptr };
};
}

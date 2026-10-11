module;
#include <expected>
#include <memory>
#include <string>
#include <vector>

export module aegis.rhi:context;
import :error;
import :device;
import aegis.platform;
import vulkan_hpp;

export namespace aegis::rhi
{
class Context
{
public:
    struct Desc
    {
        std::string_view appName;
        /// @brief nullptr runs headless: no surface, and no window-system instance extensions.
        const platform::Window* window{ nullptr };
    };

    static constexpr std::uint32_t vulkanVersion{ vk::makeApiVersion(0, 1, 3, 0) };
#ifdef NDEBUG
    static constexpr bool enableValidation{ false };
#else
    static constexpr bool enableValidation{ true };
#endif

    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<std::unique_ptr<Context>, Error>;

    /// @brief Number of validation-layer errors reported so far (always 0 without validation).
    ///        Lets a test harness fail on errors instead of relying on someone reading stderr.
    [[nodiscard]] static auto validationErrorCount() noexcept -> std::uint32_t;

    Context(
        vk::raii::Context context,
        vk::raii::Instance instance,
        vk::raii::DebugUtilsMessengerEXT messenger,
        vk::raii::SurfaceKHR surface);

    [[nodiscard]] auto instance() const -> const vk::raii::Instance& { return m_instance; }
    [[nodiscard]] auto surface() const -> const vk::raii::SurfaceKHR& { return m_surface; }
    [[nodiscard]] auto headless() const noexcept -> bool { return !*m_surface; }

    [[nodiscard]] auto createDevice(const Device::Desc& desc) const
        -> std::expected<std::unique_ptr<Device>, Error>;

private:
    [[nodiscard]] static auto createInstance(
        const vk::raii::Context& context,
        const Desc& desc)
        -> std::expected<vk::raii::Instance, Error>;
    [[nodiscard]] static auto createDebugMessenger(const vk::raii::Instance& instance)
        -> std::expected<vk::raii::DebugUtilsMessengerEXT, Error>;
    [[nodiscard]] static auto createSurface(
        const vk::raii::Instance& instance,
        const Desc& desc)
        -> std::expected<vk::raii::SurfaceKHR, Error>;
    [[nodiscard]] static auto findExtensions(const Desc& desc) -> std::vector<const char*>;
    [[nodiscard]] static auto findLayers(const vk::raii::Context& context)
        -> std::expected<std::vector<const char*>, Error>;

    vk::raii::Context m_context;
    vk::raii::Instance m_instance;
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger;
    vk::raii::SurfaceKHR m_surface;
};
}

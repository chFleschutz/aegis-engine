module;
#include <expected>
#include <string_view>

export module aegis.rhi:buffer;
import :common;
import :fwd;
import :memory;
import vulkan_hpp;

export namespace aegis::rhi
{
class Buffer
{
    friend Device;

public:
    struct Desc
    {
        std::string_view name;
        std::size_t size;
        BufferUsage usage;
        MemoryUsage memory;
    };

    [[nodiscard]] auto handle() const -> vk::Buffer { return *m_allocation; }

    /// @brief Writes 'size' bytes from 'src' to the internal buffer starting at 'offset'.
    /// @note This function needs access to the internal Buffer.
    /// @warning Only valid for memory usage of CpuWrite.
    auto write(const void* src, std::size_t size, std::size_t offset = 0) const -> void;

    /// @brief Writes 'size' bytes from the internal buffer starting at 'offset' to 'dst'.
    /// @note This function needs access to the internal Buffer.
    /// @warning Only valid for memory usage of CpuRead.
    auto read(void* dst, std::size_t size, std::size_t offset = 0) const -> void;

private:
    [[nodiscard]] static auto create(const Device& device, const Desc& desc)
        -> std::expected<Buffer, Error>;

    Buffer(
        BufferAllocation,
        vk::DeviceSize size,
        void* mappedData);

    BufferAllocation m_allocation;
    vk::DeviceSize m_size;
    void* m_mappedData;
    vk::MemoryPropertyFlags m_memoryFlags;
};
}

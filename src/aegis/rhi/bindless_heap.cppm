module;
#include <cassert>
#include <cstdint>
#include <expected>
#include <vector>

export module aegis.rhi:bindless_heap;
import :error;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class FreeList
{
public:
    explicit FreeList(std::uint32_t capacity) :
        m_capacity(capacity)
    {
    }

    void push(std::uint32_t index)
    {
        m_free.emplace_back(index);
    }

    auto pop() -> std::uint32_t
    {
        if (!m_free.empty())
        {
            auto index = m_free.back();
            m_free.pop_back();
            return index;
        }

        assert(m_head < m_capacity && "Free list exceeds max capacity");
        return m_head++;
    }

private:
    std::vector<std::uint32_t> m_free{};
    std::uint32_t m_head{ 0 };
    std::uint32_t m_capacity;
};


class BindlessHeap
{
public:
    struct Desc
    {
        std::uint32_t maxSampledImages{ 16384 };
        std::uint32_t maxStorageImages{ 4096 };
        std::uint32_t maxSamplers{ 2048 };
    };

    static constexpr std::uint32_t SampledImagesBinding = 0;
    static constexpr std::uint32_t StorageImagesBinding = 1;
    static constexpr std::uint32_t SamplersBinding = 2;

    [[nodiscard]] static auto create(const Device& device, const Desc& desc)
        -> std::expected<BindlessHeap, Error>;

    [[nodiscard]] auto descriptorSet() const -> vk::DescriptorSet { return *m_set; }
    [[nodiscard]] auto layout() const -> vk::DescriptorSetLayout { return *m_layout; }

private:
    BindlessHeap(const Desc& desc, vk::raii::DescriptorPool&& pool, vk::raii::DescriptorSetLayout&& layout,
        vk::raii::DescriptorSet&& set);

    [[nodiscard]] static auto createLayout(const Device& device, const Desc& desc)
        -> std::expected<vk::raii::DescriptorSetLayout, Error>;

    [[nodiscard]] static auto createPool(const Device& device, const Desc& desc)
        -> std::expected<vk::raii::DescriptorPool, Error>;

    [[nodiscard]] static auto createSet(const Device& device, vk::DescriptorPool pool,
        vk::DescriptorSetLayout layout)
        -> std::expected<vk::raii::DescriptorSet, Error>;

    vk::raii::DescriptorPool m_pool;
    vk::raii::DescriptorSetLayout m_layout;
    vk::raii::DescriptorSet m_set;
    FreeList m_freeSampledImages;
    FreeList m_freeStorageImages;
    FreeList m_freeSamplers;
};
}

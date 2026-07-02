module;
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

export module aegis.rhi:bindless_heap;
import :common;
import :error;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
template<DescriptorType T>
struct DescriptorHandle
{
    std::uint32_t index;
};

using SampledImageHandle = DescriptorHandle<DescriptorType::SampledImage>;
using StorageImageHandle = DescriptorHandle<DescriptorType::StorageImage>;
using SamplerHandle = DescriptorHandle<DescriptorType::Sampler>;

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

    auto pop() -> std::optional<std::uint32_t>
    {
        if (!m_free.empty())
        {
            auto index = m_free.back();
            m_free.pop_back();
            return index;
        }

        if (m_head < m_capacity)
            return std::nullopt;

        return m_head++;
    }

private:
    std::vector<std::uint32_t> m_free{};
    std::uint32_t m_head{ 0 };
    std::uint32_t m_capacity;
};


enum class BindlessError : std::uint32_t
{
    OutOfMemory = 0,
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

    [[nodiscard]] static auto create(const vk::raii::Device& device, const Desc& desc)
        -> std::expected<BindlessHeap, Error>;

    [[nodiscard]] static auto binding(DescriptorType type) -> std::uint32_t;

    [[nodiscard]] auto set() const -> vk::DescriptorSet { return *m_set; }
    [[nodiscard]] auto layout() const -> vk::DescriptorSetLayout { return *m_layout; }

    [[nodiscard]] auto writeSampledImage(const Device& device, const ImageView& view)
        -> std::expected<SampledImageHandle, BindlessError>;

    [[nodiscard]] auto writeStorageImage(const Device& device, const ImageView& view)
        -> std::expected<StorageImageHandle, BindlessError>;

    [[nodiscard]] auto writeSampler(const Device& device, vk::Sampler sampler)
        -> std::expected<SamplerHandle, BindlessError>;

    auto freeSampledImage(SampledImageHandle handle) -> void;
    auto freeStorageImage(StorageImageHandle handle) -> void;
    auto freeSampler(SamplerHandle handle) -> void;

private:
    BindlessHeap(const Desc& desc, vk::raii::DescriptorPool&& pool, vk::raii::DescriptorSetLayout&& layout,
        vk::raii::DescriptorSet&& set);

    [[nodiscard]] static auto createLayout(const vk::raii::Device& device, const Desc& desc)
        -> std::expected<vk::raii::DescriptorSetLayout, Error>;

    [[nodiscard]] static auto createPool(const vk::raii::Device& device, const Desc& desc)
        -> std::expected<vk::raii::DescriptorPool, Error>;

    [[nodiscard]] static auto createSet(const vk::raii::Device& device, vk::DescriptorPool pool,
        vk::DescriptorSetLayout layout)
        -> std::expected<vk::raii::DescriptorSet, Error>;

    auto updateDescriptorSet(const Device& device, DescriptorType type, std::uint32_t index,
        const vk::DescriptorImageInfo& info) const -> void;

    vk::raii::DescriptorPool m_pool;
    vk::raii::DescriptorSetLayout m_layout;
    vk::raii::DescriptorSet m_set;
    FreeList m_freeSampledImages;
    FreeList m_freeStorageImages;
    FreeList m_freeSamplers;
};
}

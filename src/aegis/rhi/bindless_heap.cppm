export module aegis.rhi:bindless_heap;
import std;
import :common;
import :error;
import :fwd;
import vulkan;

export namespace aegis::rhi
{
template<DescriptorType T>
struct Descriptor
{
    std::uint32_t index;
};

using SampledImageDescriptor = Descriptor<DescriptorType::SampledImage>;
using StorageImageDescriptor = Descriptor<DescriptorType::StorageImage>;
using SamplerDescriptor = Descriptor<DescriptorType::Sampler>;

/// @brief A sampled image plus the sampler to read it with: bindless::TextureDescriptor in Slang, 8 bytes
///        (image index, then sampler index). How an assets::Texture reaches a shader, and the layout of
///        a texture parameter in a material's block.
struct TextureDescriptor
{
    SampledImageDescriptor image;
    SamplerDescriptor sampler;
};

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
            return m_head++;

        return std::nullopt;
    }

private:
    std::vector<std::uint32_t> m_free{};
    std::uint32_t m_head{ 0 };
    std::uint32_t m_capacity;
};

namespace detail
{
struct ImageSlots
{
    std::optional<SampledImageDescriptor> sampled;
    std::optional<StorageImageDescriptor> storage;
};

// Picks the slots for an image view: the old descriptor when there is one (a replaced view keeps its
// slot), else a fresh slot from the free list. All or nothing: when the storage slot cannot be
// allocated, a sampled slot allocated by this call goes back to its free list, so a failed view
// creation never leaks a slot. Exhaustion reports ErrorCode::OutOfDescriptors.
[[nodiscard]] inline auto acquireImageSlots(FreeList& sampledSlots, FreeList& storageSlots, bool wantSampled,
    bool wantStorage, std::optional<SampledImageDescriptor> oldSampled,
    std::optional<StorageImageDescriptor> oldStorage) -> std::expected<ImageSlots, Error>
{
    ImageSlots slots;
    bool freshSampled = false;

    if (wantSampled)
    {
        if (oldSampled)
        {
            slots.sampled = oldSampled;
        }
        else
        {
            auto index = sampledSlots.pop();
            if (!index)
                return makeError(ErrorCode::OutOfDescriptors);
            slots.sampled = SampledImageDescriptor{ *index };
            freshSampled = true;
        }
    }

    if (wantStorage)
    {
        if (oldStorage)
        {
            slots.storage = oldStorage;
        }
        else
        {
            auto index = storageSlots.pop();
            if (!index)
            {
                if (freshSampled)
                    sampledSlots.push(slots.sampled->index);
                return makeError(ErrorCode::OutOfDescriptors);
            }
            slots.storage = StorageImageDescriptor{ *index };
        }
    }

    return slots;
}
}

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

    [[nodiscard]] auto set() const -> vk::DescriptorSet { return m_set; }
    [[nodiscard]] auto layout() const -> vk::DescriptorSetLayout { return *m_layout; }

    // Allocates the descriptors a view needs and writes them (the one place that does so). With
    // `old`, descriptors it already holds are reused and rewritten for the new view. On failure
    // nothing stays allocated and the view is unchanged.
    [[nodiscard]] auto writeImageView(const Device& device, ImageView& view, ImageUsage usage,
        const ImageView* old = nullptr) -> std::expected<void, Error>;

    [[nodiscard]] auto writeSampler(const Device& device, vk::Sampler sampler)
        -> std::expected<SamplerDescriptor, Error>;

    auto freeSampledImage(SampledImageDescriptor descriptor) -> void;
    auto freeStorageImage(StorageImageDescriptor descriptor) -> void;
    auto freeSampler(SamplerDescriptor descriptor) -> void;

private:
    BindlessHeap(const Desc& desc, vk::raii::DescriptorPool&& pool, vk::raii::DescriptorSetLayout&& layout,
        vk::DescriptorSet set);

    [[nodiscard]] static auto createLayout(const vk::raii::Device& device, const Desc& desc)
        -> std::expected<vk::raii::DescriptorSetLayout, Error>;

    [[nodiscard]] static auto createPool(const vk::raii::Device& device, const Desc& desc)
        -> std::expected<vk::raii::DescriptorPool, Error>;

    [[nodiscard]] static auto createSet(const vk::raii::Device& device, vk::DescriptorPool pool,
        vk::DescriptorSetLayout layout)
        -> std::expected<vk::DescriptorSet, Error>;

    auto writeSampledImage(const Device& device, const ImageView& view, SampledImageDescriptor descriptor) const -> void;
    auto writeStorageImage(const Device& device, const ImageView& view, StorageImageDescriptor descriptor) const -> void;

    auto updateDescriptorSet(const Device& device, DescriptorType type, std::uint32_t index,
        const vk::DescriptorImageInfo& info) const -> void;

    vk::raii::DescriptorPool m_pool;
    vk::raii::DescriptorSetLayout m_layout;
    vk::DescriptorSet m_set;
    FreeList m_freeSampledImages;
    FreeList m_freeStorageImages;
    FreeList m_freeSamplers;
};
}

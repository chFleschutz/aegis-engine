module;
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module aegis.render_graph:schedule;
import :resource;

// The frame graph's pure logic, separate from any GPU object so it can be unit tested.

export namespace aegis::render_graph::detail
{
/// @brief A pass's declared resource entries (indices into the resource pool).
struct PassDecl
{
    std::vector<std::uint32_t> reads;
    std::vector<std::uint32_t> writes;
};

/// @brief A resource pool entry: either a resource it creates, or a reference (by name) to one.
struct EntryDecl
{
    std::optional<std::uint32_t> referenceTo; // The created entry this reference resolved to
};

/// @brief Orders passes so every pass runs after the passes producing what it reads or overwrites.
///        A pass creating a resource produces it; a later pass writing a reference to it becomes its
///        producer from then on (in declaration order), and readers depend on the last producer.
/// @return Pass indices in execution order, or an error naming a dependency cycle.
[[nodiscard]] auto sortPasses(std::span<const PassDecl> passes, std::span<const EntryDecl> entries)
    -> std::expected<std::vector<std::uint32_t>, std::string>;

enum class ResourceKind
{
    Image,
    Buffer,
};

/// @brief One access of a resource by a pass, in the order the pass performs them.
struct Access
{
    std::uint32_t resource; // A created entry (references already resolved)
    Usage usage;
};

/// @brief A barrier a pass records before executing. 'from' is nullopt for an image's first use in a
///        frame: its previous contents are discarded (every graph image is produced each frame).
struct Transition
{
    std::uint32_t resource;
    std::optional<Usage> from;
    Usage to;

    auto operator==(const Transition&) const -> bool = default;
};

/// @brief Plans the barriers of one frame. 'passes' holds each pass's accesses, in execution order.
///
/// - An image's first access each frame transitions from undefined.
/// - A buffer's first access synchronizes with its last access of the previous frame, which may
///   still run on the GPU (same queue, so a barrier suffices).
/// - Later accesses get a barrier when either side writes, or (images) when the layout changes.
/// - HostWrite is no GPU access: the queue submission makes host writes visible.
[[nodiscard]] auto planTransitions(std::span<const std::vector<Access>> passes,
    std::span<const ResourceKind> resources) -> std::vector<std::vector<Transition>>;
}

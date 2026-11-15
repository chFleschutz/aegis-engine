export module aegis.render_graph:schedule;
import std;
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

/// @brief Orders passes by their resource accesses, following declaration order. A pass creating a
///        resource produces it first, wherever it is declared. After that, in declaration order, a
///        reader runs after the previous writer and before the next one, and a writer after the
///        previous writer. Passes without such a relation keep their add order.
/// @return Pass indices in execution order, or an error naming a dependency cycle.
[[nodiscard]] auto sortPasses(std::span<const PassDecl> passes, std::span<const EntryDecl> entries)
    -> std::expected<std::vector<std::uint32_t>, std::string>;

enum class ResourceKind
{
    Image,
    Buffer,
};

/// @brief What planning the barriers needs to know of a created or imported resource.
struct PlannedResource
{
    ResourceKind kind;
    Lifetime lifetime{ Lifetime::Frame }; // Of an image the graph creates
    bool imported{ false };
    std::optional<Usage> before; // Imported: its last access when the frame starts
    std::optional<Usage> after;  // Imported: the access it is left ready for
};

/// @brief One access of a resource by a pass, in the order the pass performs them.
struct Access
{
    std::uint32_t resource; // A created entry (references already resolved)
    Usage usage;
    bool previous{ false }; // Of a history pair: the previous frame's image, not the current one
};

/// @brief A barrier a pass records before executing. 'from' is nullopt when the image's previous contents are
///        discarded.
struct Transition
{
    std::uint32_t resource;
    std::optional<Usage> from;
    Usage to;
    /// @brief The barrier is for the previous frame's image of a history pair.
    bool previous{ false };
    /// @brief 'from' is an access of the previous frame. A fresh image has none: the graph then transitions it
    ///        from undefined instead.
    bool crossFrame{ false };

    auto operator==(const Transition&) const -> bool = default;
};

/// @brief Plans the barriers of one frame. 'passes' holds each pass's accesses, in execution order.
///
/// - A Lifetime::Frame image's first access each frame transitions from undefined.
/// - A buffer's first access synchronizes with its last access of the previous frame, which may
///   still run on the GPU (same queue, so a barrier suffices). So does a persistent texture's.
/// - The images of a history pair swap, so each was the other a frame ago: the first access of the previous
///   frame's image synchronizes with the last access of the current one, and the first access of the current
///   image with the last access of the previous frame's (from undefined when no pass reads that).
/// - An imported resource's first access follows its 'before' access.
/// - Later accesses get a barrier when either side writes, or (images) when the layout changes.
/// - HostWrite is no GPU access: the queue submission makes host writes visible.
[[nodiscard]] auto planTransitions(std::span<const std::vector<Access>> passes,
    std::span<const PlannedResource> resources) -> std::vector<std::vector<Transition>>;

/// @brief The barriers recorded after the last pass: each imported resource the frame accessed is left ready
///        for its 'after' access.
[[nodiscard]] auto planEndOfFrame(std::span<const std::vector<Access>> passes,
    std::span<const PlannedResource> resources) -> std::vector<Transition>;
}

export module aegis.editor.core:settled_extent;
import std;

export namespace aegis::editor
{
/// @brief A size in pixels.
struct PixelSize
{
    std::uint32_t width{ 0 };
    std::uint32_t height{ 0 };

    [[nodiscard]] auto operator==(const PixelSize&) const -> bool = default;
};

/// @brief The size a viewport renders at: it follows the panel's size once that has stayed the same for a few
///        frames, so dragging a panel's edge doesn't resize the frame graph every frame. The first size applies
///        at once.
class SettledExtent
{
public:
    /// @brief Frames a new size has to stay before it applies.
    static constexpr std::uint32_t settleFrames{ 5 };

    /// @brief This frame's panel size, at least 1x1; returns the size to render at.
    auto update(PixelSize requested) -> PixelSize;

    /// @brief The size to render at, or none before the first update or after a reset.
    [[nodiscard]] auto current() const -> std::optional<PixelSize> { return m_current; }
    /// @brief Forgets the size, as when the panel closes; the next update applies at once.
    void reset();

private:
    std::optional<PixelSize> m_current;
    PixelSize m_pending;
    std::uint32_t m_stableFrames{ 0 };
};
}

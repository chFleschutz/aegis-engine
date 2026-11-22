module aegis.editor:statistics;
import std;
import aegis.editor.core;
import aegis.engine;

// The numbers the Statistics panel and the viewport's stats overlay show, and the check that they describe the world
// the panels show.
namespace aegis::editor
{
/// @brief The Statistics panel's summary numbers, which the viewport's stats overlay shows too.
struct StatisticsSummary
{
    std::size_t entities{ 0 };
    std::uint32_t drawBatches{ 0 };
    std::uint32_t instances{ 0 };
};

/// @brief The summary of the world the panels show and of its instances; zeros without a world.
[[nodiscard]] auto statisticsSummary(const EditorContext& editor, const FrameContext& frame) -> StatisticsSummary;

/// @brief --editor-check-stats: proves that during play the statistics describe the play world, not the edit world.
///        In the first frame of play it adds an instance that draws nothing to the play world (a copy of a mesh at scale
///        0, which leaves the image and the scene's bounds as they were), so the two worlds' counts differ; a few frames
///        later, once the play world has rendered it, it compares statisticsSummary with both worlds. A failure, also of
///        a run that ends before the check ran, is logged as "Editor statistics check failed: ...".
class StatisticsCheck
{
public:
    /// @brief Takes the check's next step; call once a frame, after the panels drew.
    void onFrame(const EditorContext& editor, FrameContext& frame);
    /// @brief Logs the failure of a check that hasn't finished.
    void onShutdown() const;

private:
    enum class Step
    {
        Add,
        Verify,
        Done,
    };

    Step m_step{ Step::Add };
    int m_waitFrames{ 0 }; // Before Verify, until the play world has rendered the copy
};
}

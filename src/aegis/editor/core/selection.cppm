export module aegis.editor.core:selection;
import std;
import aegis.world;

export namespace aegis::editor
{
/// @brief The selected entities, by StableId, and the active one (the last selected). An id stays valid when
///        its entity is destroyed and recreated with the same id, which an entity handle does not.
class Selection
{
public:
    /// @brief Selects only 'id', which becomes the active one.
    void select(StableId id);
    /// @brief Adds 'id' to the selection and makes it the active one.
    void add(StableId id);
    void remove(StableId id);
    void clear();

    [[nodiscard]] auto contains(StableId id) const -> bool;
    [[nodiscard]] auto empty() const -> bool { return m_ids.empty(); }
    /// @brief The selected ids in the order they were selected; the active one is last.
    [[nodiscard]] auto ids() const -> std::span<const StableId> { return m_ids; }
    /// @brief The last selected id, or none.
    [[nodiscard]] auto active() const -> std::optional<StableId>;

    /// @brief Drops the ids whose entity no longer exists in 'registry'.
    void prune(const world::Registry& registry);

private:
    std::vector<StableId> m_ids;
};
}

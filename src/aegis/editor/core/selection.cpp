module aegis.editor.core;
import std;

namespace aegis::editor
{
void Selection::select(StableId id)
{
    m_ids.assign(1, id);
}

void Selection::add(StableId id)
{
    remove(id);
    m_ids.push_back(id);
}

void Selection::remove(StableId id)
{
    std::erase(m_ids, id);
}

void Selection::clear()
{
    m_ids.clear();
}

auto Selection::contains(StableId id) const -> bool
{
    return std::ranges::contains(m_ids, id);
}

auto Selection::active() const -> std::optional<StableId>
{
    if (m_ids.empty())
        return std::nullopt;
    return m_ids.back();
}

void Selection::prune(const world::Registry& registry)
{
    std::erase_if(m_ids, [&registry](StableId id) { return !registry.find(id); });
}
}

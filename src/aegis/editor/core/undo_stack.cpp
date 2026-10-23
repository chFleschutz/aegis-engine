module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.world;
import aegis.world.types;

namespace aegis::editor
{
namespace
{
// Whether two values are the same; aegis.math does not export the glm vector operator==, so those compare per component.
auto sameValue(const reflect::Value& a, const reflect::Value& b) -> bool
{
    return a.index() == b.index() && std::visit(
                                         [&b](const auto& left) {
                                             using T = std::decay_t<decltype(left)>;
                                             const auto& right = std::get<T>(b);
                                             if constexpr (std::equality_comparable<T>)
                                             {
                                                 return left == right;
                                             }
                                             else
                                             {
                                                 for (int i = 0; i < T::length(); ++i)
                                                 {
                                                     if (left[i] != right[i])
                                                         return false;
                                                 }
                                                 return true;
                                             }
                                         },
                                         a);
}
}

void UndoStack::record(FieldEdit edit, bool continuous)
{
    edit.open = continuous;
    if (m_applied > 0 && m_applied == m_edits.size() && m_edits.back().open && m_edits.back().sameTarget(edit))
    {
        // The drag goes on: one entry from the value before it to the latest
        m_edits.back().newValue = std::move(edit.newValue);
        m_edits.back().open = continuous;
        return;
    }
    if (sameValue(edit.oldValue, edit.newValue))
        return;

    m_edits.resize(m_applied);
    if (m_saved && *m_saved > m_applied)
        m_saved.reset();
    m_edits.push_back(std::move(edit));
    ++m_applied;
    if (m_edits.size() > maxDepth)
    {
        m_edits.erase(m_edits.begin());
        --m_applied;
        if (m_saved)
            m_saved = *m_saved > 0 ? std::optional{ *m_saved - 1 } : std::nullopt;
    }
}

void UndoStack::seal()
{
    if (!m_edits.empty())
        m_edits.back().open = false;
}

auto UndoStack::undo() -> const FieldEdit*
{
    if (!canUndo())
        return nullptr;
    seal();
    return &m_edits[--m_applied];
}

auto UndoStack::redo() -> const FieldEdit*
{
    return canRedo() ? &m_edits[m_applied++] : nullptr;
}

void UndoStack::markSaved()
{
    seal();
    m_saved = m_applied;
}

void UndoStack::clear()
{
    m_edits.clear();
    m_applied = 0;
    m_saved = 0;
}

auto applyEdit(const reflect::TypeRegistry& types, world::Registry& registry, const FieldEdit& edit, EditSide side,
    const std::function<void(const reflect::TypeInfo& type, void* object)>& prepare, const FindScript& findScript) -> std::expected<void, reflect::Error>
{
    const auto* type = types.find(edit.type);
    if (!type)
        return reflect::makeError(reflect::ErrorCode::UnknownType, std::format("no type '{}'", edit.type));
    const auto* field = type->findField(edit.field);
    if (!field)
        return reflect::makeError(reflect::ErrorCode::UnknownField, std::format("'{}' has no field '{}'", edit.type, edit.field));

    const auto entity = registry.find(edit.entity);
    if (!entity)
        return reflect::makeError(reflect::ErrorCode::WrongCategory, std::format("the entity of the edit of '{}.{}' is gone", edit.type, edit.field));

    void* object = nullptr;
    if (edit.scriptIndex)
        object = findScript ? findScript(*type, registry, entity, *edit.scriptIndex) : nullptr;
    else if (const auto* storage = type->attachment<world::ComponentStorage>())
        object = storage->find(registry, entity);
    if (!object)
        return reflect::makeError(reflect::ErrorCode::WrongCategory, std::format("the entity no longer has '{}'", edit.type));

    if (auto written = field->setter(object, side == EditSide::Old ? edit.oldValue : edit.newValue); !written)
        return written;
    if (prepare)
        prepare(*type, object);
    if (const auto* storage = type->attachment<world::ComponentStorage>())
        storage->changed(registry, entity);
    return {};
}
}

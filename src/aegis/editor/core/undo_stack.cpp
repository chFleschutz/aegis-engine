module aegis.editor.core;
import std;

namespace aegis::editor
{
namespace
{
// Field, component and rename edits merge while continuous; they have an 'open' flag and a target.
template<class T>
concept Mergeable = requires(T edit) {
    { edit.open } -> std::convertible_to<bool>;
    { edit.sameTarget(edit) } -> std::same_as<bool>;
};

// Takes the new side of 'next', the same edit continued.
void extendWith(FieldEdit& edit, FieldEdit&& next)
{
    edit.newValue = std::move(next.newValue);
}
void extendWith(ComponentEdit& edit, ComponentEdit&& next)
{
    edit.newValues = std::move(next.newValues);
}
void extendWith(RenameEdit& edit, RenameEdit&& next)
{
    edit.newName = std::move(next.newName);
}

void setOpen(Edit& edit, bool open)
{
    std::visit(
        [open](auto& entry) {
            if constexpr (Mergeable<std::decay_t<decltype(entry)>>)
                entry.open = open;
        },
        edit);
}

// Extends 'last' with 'next' if 'last' is still open on the same target.
auto extend(Edit& last, Edit& next, bool continuous) -> bool
{
    return std::visit(
        [&next, continuous](auto& previous) {
            using T = std::decay_t<decltype(previous)>;
            if constexpr (Mergeable<T>)
            {
                auto* following = std::get_if<T>(&next);
                if (!following || !previous.open || !previous.sameTarget(*following))
                    return false;
                extendWith(previous, std::move(*following));
                previous.open = continuous;
                return true;
            }
            else
            {
                return false;
            }
        },
        last);
}

auto changesNothing(const Edit& edit) -> bool
{
    if (const auto* field = std::get_if<FieldEdit>(&edit))
        return sameValue(field->oldValue, field->newValue);
    if (const auto* component = std::get_if<ComponentEdit>(&edit))
        return sameValues(component->oldValues, component->newValues);
    if (const auto* rename = std::get_if<RenameEdit>(&edit))
        return rename->oldName == rename->newName;
    if (const auto* move = std::get_if<MoveEdit>(&edit))
        return move->oldPlace.parent == move->newPlace.parent && (!move->newPlace.parent || move->oldPlace.index == move->newPlace.index);
    return false;
}
}

void UndoStack::record(Edit edit, bool continuous)
{
    setOpen(edit, continuous);
    // The drag goes on: one entry from the value before it to the latest
    if (m_applied > 0 && m_applied == m_edits.size() && extend(m_edits.back(), edit, continuous))
        return;
    if (changesNothing(edit))
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
        setOpen(m_edits.back(), false);
}

auto UndoStack::undo() -> const Edit*
{
    if (!canUndo())
        return nullptr;
    seal();
    return &m_edits[--m_applied];
}

auto UndoStack::redo() -> const Edit*
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
}

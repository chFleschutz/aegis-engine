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
    // The entry this edit may extend: the open group's once it has one, or the last one outside a group
    const bool grouped = m_groupDepth > 0;
    if ((!grouped || m_groupRecorded) && m_applied > 0 && m_applied == m_entries.size())
    {
        // The drag goes on: one edit from the value before it to the latest
        auto& edits = m_entries.back().edits;
        for (auto& previous : std::views::reverse(edits))
        {
            if (extend(previous, edit, continuous))
                return;
        }
    }
    if (changesNothing(edit))
        return;

    if (grouped && m_groupRecorded && m_applied == m_entries.size())
    {
        m_entries.back().edits.push_back(std::move(edit));
        return;
    }
    Entry entry;
    entry.edits.push_back(std::move(edit));
    if (grouped)
        entry.title = m_groupTitle;
    push(std::move(entry));
    m_groupRecorded = grouped;
}

void UndoStack::push(Entry entry)
{
    m_entries.resize(m_applied);
    if (m_saved && *m_saved > m_applied)
        m_saved.reset();
    m_entries.push_back(std::move(entry));
    ++m_applied;
    if (m_entries.size() > maxDepth)
    {
        m_entries.erase(m_entries.begin());
        --m_applied;
        if (m_saved)
            m_saved = *m_saved > 0 ? std::optional{ *m_saved - 1 } : std::nullopt;
    }
}

void UndoStack::seal()
{
    if (m_entries.empty())
        return;
    for (auto& edit : m_entries.back().edits)
        setOpen(edit, false);
}

void UndoStack::beginGroup(std::string title)
{
    if (m_groupDepth++ > 0)
        return; // Joins the open group
    seal();     // Nothing before the group extends into it
    m_groupTitle = std::move(title);
    m_groupRecorded = false;
}

void UndoStack::endGroup()
{
    if (m_groupDepth == 0 || --m_groupDepth > 0)
        return;
    seal(); // Nothing after the group extends into it
    m_groupTitle.clear();
    m_groupRecorded = false;
}

auto UndoStack::undo() -> std::span<const Edit>
{
    if (!canUndo())
        return {};
    seal();
    m_groupRecorded = false;
    return m_entries[--m_applied].edits;
}

auto UndoStack::redo() -> std::span<const Edit>
{
    if (!canRedo())
        return {};
    m_groupRecorded = false;
    return m_entries[m_applied++].edits;
}

void UndoStack::markSaved()
{
    seal();
    m_saved = m_applied;
}

void UndoStack::markUnsaved()
{
    seal();
    m_saved.reset();
}

void UndoStack::clear()
{
    m_entries.clear();
    m_applied = 0;
    m_saved = 0;
    m_groupRecorded = false;
}
}

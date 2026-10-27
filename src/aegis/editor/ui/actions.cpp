module;

#include <cassert>

#include <imgui.h>

module aegis.editor;
import std;
import :actions;

namespace aegis::editor
{
namespace
{
auto toImGui(Key key) -> ImGuiKey
{
    switch (key)
    {
    case Key::D:
        return ImGuiKey_D;
    case Key::F:
        return ImGuiKey_F;
    case Key::N:
        return ImGuiKey_N;
    case Key::O:
        return ImGuiKey_O;
    case Key::P:
        return ImGuiKey_P;
    case Key::S:
        return ImGuiKey_S;
    case Key::Y:
        return ImGuiKey_Y;
    case Key::Z:
        return ImGuiKey_Z;
    case Key::Delete:
        return ImGuiKey_Delete;
    case Key::F2:
        return ImGuiKey_F2;
    }
    std::unreachable();
}

auto canRun(const Action& action) -> bool
{
    return !action.enabled || action.enabled();
}

// Whether 'chord' was pressed this frame, with exactly its modifiers.
auto pressed(const Chord& chord, bool repeats) -> bool
{
    const auto& io = ImGui::GetIO();
    return io.KeyCtrl == chord.ctrl && io.KeyShift == chord.shift && io.KeyAlt == chord.alt && ImGui::IsKeyPressed(toImGui(chord.key), repeats);
}
}

void ActionRegistry::add(Action action)
{
    assert(!find(action.id) && "An action id is used twice");
    for (const auto& other : m_actions)
    {
        for (const auto& chord : { action.chord, action.alternate })
        {
            assert((!chord || (other.chord != chord && other.alternate != chord)) && "Two actions share a shortcut");
        }
    }
    m_actions.push_back(std::move(action));
}

auto ActionRegistry::find(std::string_view id) const -> const Action*
{
    const auto it = std::ranges::find(m_actions, id, &Action::id);
    return it == m_actions.end() ? nullptr : &*it;
}

auto ActionRegistry::shortcut(std::string_view id) const -> std::string
{
    const auto* action = find(id);
    return action && action->chord ? describe(*action->chord) : std::string{};
}

auto ActionRegistry::tooltip(std::string_view id) const -> std::string
{
    const auto* action = find(id);
    if (!action)
        return {};
    return action->chord ? std::format("{} ({})", action->label, describe(*action->chord)) : action->label;
}

void ActionRegistry::menuItem(std::string_view id, FrameContext& frame, std::string_view label) const
{
    const auto* action = find(id);
    assert(action && "Unknown action");
    if (!action)
        return;
    const std::string text{ label.empty() ? std::string_view{ action->label } : label };
    const std::string chord = shortcut(id);
    if (ImGui::MenuItem(text.c_str(), chord.empty() ? nullptr : chord.c_str(), false, canRun(*action)))
        action->run(frame);
}

void ActionRegistry::handleShortcuts(FrameContext& frame) const
{
    if (ImGui::GetIO().WantTextInput)
        return;
    for (const auto& action : m_actions)
    {
        const bool hit = (action.chord && pressed(*action.chord, action.repeats)) || (action.alternate && pressed(*action.alternate, action.repeats));
        if (hit && canRun(action))
        {
            action.run(frame);
            return;
        }
    }
}
}

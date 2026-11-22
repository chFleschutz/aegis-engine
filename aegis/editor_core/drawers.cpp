module aegis.editor.core;
import std;

namespace aegis::editor
{
namespace
{
constexpr std::string_view pinnedKey = "Pinned=";
}

auto Drawers::shown() const -> std::optional<std::string_view>
{
    return m_shown ? std::optional<std::string_view>{ *m_shown } : std::nullopt;
}

auto Drawers::isPinned(std::string_view panel) const -> bool
{
    return std::ranges::find(m_pinned, panel) != m_pinned.end();
}

auto Drawers::show(std::string_view panel) -> bool
{
    if (isPinned(panel))
        return false;
    m_shown = std::string{ panel };
    return true;
}

auto Drawers::toggle(std::string_view panel) -> bool
{
    if (isShown(panel))
    {
        close();
        return true;
    }
    return show(panel);
}

void Drawers::hide(std::string_view panel)
{
    if (isShown(panel))
        close();
}

void Drawers::pin(std::string_view panel)
{
    std::string name{ panel }; // 'panel' may be shown(), which hide clears
    hide(name);
    if (!isPinned(name))
        m_pinned.push_back(std::move(name));
}

void Drawers::unpin(std::string_view panel)
{
    const std::string name{ panel }; // 'panel' may be one of pinned(), which the erase moves
    std::erase(m_pinned, name);
    show(name);
}

void Drawers::reset()
{
    m_pinned.clear();
    close();
}

auto Drawers::settingsLine() const -> std::string
{
    std::string line{ pinnedKey };
    for (const auto& panel : m_pinned)
    {
        if (&panel != &m_pinned.front())
            line += ',';
        line += panel;
    }
    return line;
}

void Drawers::readSettingsLine(std::string_view line)
{
    if (!line.starts_with(pinnedKey))
        return;
    line.remove_prefix(pinnedKey.size());
    m_pinned.clear();
    for (const auto part : std::views::split(line, ','))
    {
        const std::string_view panel{ part.begin(), part.end() };
        if (!panel.empty() && !isPinned(panel))
            m_pinned.emplace_back(panel);
    }
}
}

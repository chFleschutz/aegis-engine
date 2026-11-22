module aegis.editor.core;
import std;

namespace aegis::editor
{
namespace
{
constexpr std::string_view foldedKey = "Folded=";
}

auto SideRail::flyout() const -> std::optional<std::string_view>
{
    return m_flyout ? std::optional<std::string_view>{ *m_flyout } : std::nullopt;
}

void SideRail::setFolded(bool folded)
{
    m_folded = folded;
    close();
}

auto SideRail::show(std::string_view panel) -> bool
{
    if (!m_folded)
        return false;
    m_flyout = std::string{ panel };
    return true;
}

auto SideRail::toggle(std::string_view panel) -> bool
{
    if (isShown(panel))
    {
        close();
        return true;
    }
    return show(panel);
}

auto SideRail::settingsLine() const -> std::string
{
    return std::format("{}{}", foldedKey, m_folded ? 1 : 0);
}

void SideRail::readSettingsLine(std::string_view line)
{
    if (!line.starts_with(foldedKey))
        return;
    line.remove_prefix(foldedKey.size());
    if (line == "1" || line == "0")
        setFolded(line == "1");
}
}

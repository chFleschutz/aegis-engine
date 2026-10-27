export module aegis.editor.core:chord;
import std;

export namespace aegis::editor
{
/// @brief A key a shortcut can use. The ImGui layer maps it to its own key.
enum class Key
{
    D,
    F,
    N,
    O,
    P,
    S,
    Y,
    Z,
    Delete,
    F2,
};

/// @brief A key with the modifiers that must be held with it (exactly these: Ctrl+S is not Ctrl+Shift+S).
struct Chord
{
    Key key{ Key::S };
    bool ctrl{ false };
    bool shift{ false };
    bool alt{ false };

    [[nodiscard]] auto operator==(const Chord&) const -> bool = default;
};

/// @brief The chord as menus show it: "Ctrl+Shift+S", "Del", "F2".
[[nodiscard]] auto describe(const Chord& chord) -> std::string;
}

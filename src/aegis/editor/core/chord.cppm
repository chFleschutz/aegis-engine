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
    F1,
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

/// @brief Whether 'chord' holds Ctrl, Shift or Alt. While the game has the keyboard only these run in the editor; a
///        plain key (Del, F2, F) is the game's.
[[nodiscard]] constexpr auto hasModifier(const Chord& chord) -> bool
{
    return chord.ctrl || chord.shift || chord.alt;
}
}

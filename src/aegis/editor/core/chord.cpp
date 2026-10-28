module aegis.editor.core;
import std;

namespace aegis::editor
{
namespace
{
auto keyName(Key key) -> std::string_view
{
    switch (key)
    {
    case Key::D:
        return "D";
    case Key::F:
        return "F";
    case Key::N:
        return "N";
    case Key::O:
        return "O";
    case Key::P:
        return "P";
    case Key::S:
        return "S";
    case Key::Y:
        return "Y";
    case Key::Z:
        return "Z";
    case Key::Delete:
        return "Del";
    case Key::F2:
        return "F2";
    }
    std::unreachable();
}
}

auto describe(const Chord& chord) -> std::string
{
    std::string text;
    if (chord.ctrl)
        text += "Ctrl+";
    if (chord.shift)
        text += "Shift+";
    if (chord.alt)
        text += "Alt+";
    text += keyName(chord.key);
    return text;
}
}

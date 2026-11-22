module aegis.platform;

import std;

namespace aegis::platform
{
namespace
{
struct InputName
{
    std::string_view name;
    InputSource source;
};

// clang-format off
constexpr auto INPUT_NAMES = std::to_array<InputName>({
    { "Space", Key::Space },
    { "Apostrophe", Key::Apostrophe },
    { "Comma", Key::Comma },
    { "Minus", Key::Minus },
    { "Period", Key::Period },
    { "Slash", Key::Slash },
    { "Zero", Key::Zero },
    { "One", Key::One },
    { "Two", Key::Two },
    { "Three", Key::Three },
    { "Four", Key::Four },
    { "Five", Key::Five },
    { "Six", Key::Six },
    { "Seven", Key::Seven },
    { "Eight", Key::Eight },
    { "Nine", Key::Nine },
    { "Semicolon", Key::Semicolon },
    { "Equal", Key::Equal },
    { "A", Key::A },
    { "B", Key::B },
    { "C", Key::C },
    { "D", Key::D },
    { "E", Key::E },
    { "F", Key::F },
    { "G", Key::G },
    { "H", Key::H },
    { "I", Key::I },
    { "J", Key::J },
    { "K", Key::K },
    { "L", Key::L },
    { "M", Key::M },
    { "N", Key::N },
    { "O", Key::O },
    { "P", Key::P },
    { "Q", Key::Q },
    { "R", Key::R },
    { "S", Key::S },
    { "T", Key::T },
    { "U", Key::U },
    { "V", Key::V },
    { "W", Key::W },
    { "X", Key::X },
    { "Y", Key::Y },
    { "Z", Key::Z },
    { "LeftBracket", Key::LeftBracket },
    { "Backslash", Key::Backslash },
    { "RightBracket", Key::RightBracket },
    { "GraveAccent", Key::GraveAccent },
    { "World1", Key::World1 },
    { "World2", Key::World2 },
    { "Escape", Key::Escape },
    { "Enter", Key::Enter },
    { "Tab", Key::Tab },
    { "Backspace", Key::Backspace },
    { "Insert", Key::Insert },
    { "Delete", Key::Delete },
    { "Right", Key::Right },
    { "Left", Key::Left },
    { "Down", Key::Down },
    { "Up", Key::Up },
    { "PageUp", Key::PageUp },
    { "PageDown", Key::PageDown },
    { "Home", Key::Home },
    { "End", Key::End },
    { "CapsLock", Key::CapsLock },
    { "ScrollLock", Key::ScrollLock },
    { "NumLock", Key::NumLock },
    { "PrintScreen", Key::PrintScreen },
    { "Pause", Key::Pause },
    { "F1", Key::F1 },
    { "F2", Key::F2 },
    { "F3", Key::F3 },
    { "F4", Key::F4 },
    { "F5", Key::F5 },
    { "F6", Key::F6 },
    { "F7", Key::F7 },
    { "F8", Key::F8 },
    { "F9", Key::F9 },
    { "F10", Key::F10 },
    { "F11", Key::F11 },
    { "F12", Key::F12 },
    { "F13", Key::F13 },
    { "F14", Key::F14 },
    { "F15", Key::F15 },
    { "F16", Key::F16 },
    { "F17", Key::F17 },
    { "F18", Key::F18 },
    { "F19", Key::F19 },
    { "F20", Key::F20 },
    { "F21", Key::F21 },
    { "F22", Key::F22 },
    { "F23", Key::F23 },
    { "F24", Key::F24 },
    { "F25", Key::F25 },
    { "KeyPad0", Key::KeyPad0 },
    { "KeyPad1", Key::KeyPad1 },
    { "KeyPad2", Key::KeyPad2 },
    { "KeyPad3", Key::KeyPad3 },
    { "KeyPad4", Key::KeyPad4 },
    { "KeyPad5", Key::KeyPad5 },
    { "KeyPad6", Key::KeyPad6 },
    { "KeyPad7", Key::KeyPad7 },
    { "KeyPad8", Key::KeyPad8 },
    { "KeyPad9", Key::KeyPad9 },
    { "KeyPadDecimal", Key::KeyPadDecimal },
    { "KeyPadDivide", Key::KeyPadDivide },
    { "KeyPadMultiply", Key::KeyPadMultiply },
    { "KeyPadSubtract", Key::KeyPadSubtract },
    { "KeyPadAdd", Key::KeyPadAdd },
    { "KeyPadEnter", Key::KeyPadEnter },
    { "KeyPadEqual", Key::KeyPadEqual },
    { "LeftShift", Key::LeftShift },
    { "LeftControl", Key::LeftControl },
    { "LeftAlt", Key::LeftAlt },
    { "LeftSuper", Key::LeftSuper },
    { "RightShift", Key::RightShift },
    { "RightControl", Key::RightControl },
    { "RightAlt", Key::RightAlt },
    { "RightSuper", Key::RightSuper },
    { "Menu", Key::Menu },
    { "MouseLeft", MouseButton::Left },
    { "MouseRight", MouseButton::Right },
    { "MouseMiddle", MouseButton::Middle },
    { "MouseButton4", MouseButton::Button4 },
    { "MouseButton5", MouseButton::Button5 },
    { "MouseButton6", MouseButton::Button6 },
    { "MouseButton7", MouseButton::Button7 },
    { "MouseButton8", MouseButton::Button8 },
    { "GamepadA", GamepadButton::A },
    { "GamepadB", GamepadButton::B },
    { "GamepadX", GamepadButton::X },
    { "GamepadY", GamepadButton::Y },
    { "GamepadLeftBumper", GamepadButton::LeftBumper },
    { "GamepadRightBumper", GamepadButton::RightBumper },
    { "GamepadBack", GamepadButton::Back },
    { "GamepadStart", GamepadButton::Start },
    { "GamepadGuide", GamepadButton::Guide },
    { "GamepadLeftThumb", GamepadButton::LeftThumb },
    { "GamepadRightThumb", GamepadButton::RightThumb },
    { "GamepadDpadUp", GamepadButton::DpadUp },
    { "GamepadDpadRight", GamepadButton::DpadRight },
    { "GamepadDpadDown", GamepadButton::DpadDown },
    { "GamepadDpadLeft", GamepadButton::DpadLeft },
    { "GamepadLeftX", GamepadAxis::LeftX },
    { "GamepadLeftY", GamepadAxis::LeftY },
    { "GamepadRightX", GamepadAxis::RightX },
    { "GamepadRightY", GamepadAxis::RightY },
    { "GamepadLeftTrigger", GamepadAxis::LeftTrigger },
    { "GamepadRightTrigger", GamepadAxis::RightTrigger },
});
// clang-format on
}

auto parseInputSource(std::string_view name) -> std::optional<InputSource>
{
    const auto found = std::ranges::find(INPUT_NAMES, name, &InputName::name);
    if (found == INPUT_NAMES.end())
        return std::nullopt;
    return found->source;
}

auto inputSourceName(const InputSource& source) -> std::string_view
{
    const auto found = std::ranges::find(INPUT_NAMES, source, &InputName::source);
    return found == INPUT_NAMES.end() ? std::string_view{} : found->name;
}

auto InputMap::addAction(std::string name, std::vector<InputSource> sources) -> bool
{
    if (contains(name))
        return false;
    m_actions.emplace(std::move(name), std::move(sources));
    return true;
}

auto InputMap::addAxis(std::string name, std::vector<AxisBinding> bindings) -> bool
{
    if (contains(name))
        return false;
    m_axes.emplace(std::move(name), std::move(bindings));
    return true;
}

auto InputMap::action(std::string_view name) const -> const std::vector<InputSource>*
{
    const auto found = m_actions.find(name);
    return found == m_actions.end() ? nullptr : &found->second;
}

auto InputMap::axis(std::string_view name) const -> const std::vector<AxisBinding>*
{
    const auto found = m_axes.find(name);
    return found == m_axes.end() ? nullptr : &found->second;
}

auto InputMap::contains(std::string_view name) const -> bool
{
    return m_actions.contains(name) || m_axes.contains(name);
}
}

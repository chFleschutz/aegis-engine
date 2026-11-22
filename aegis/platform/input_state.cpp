module aegis.platform;

import std;
import aegis.core;

namespace aegis::platform
{
namespace
{
// A gamepad axis bound to an action, or to one side of an axis pair, is down past this.
constexpr float AXIS_DOWN_THRESHOLD{ 0.5f };
}

auto InputState::action(std::string_view name) const -> ActionState
{
    const auto* sources = m_map.action(name);
    if (!sources)
    {
        warnUnknown("action", name);
        return {};
    }

    bool down = false;
    bool downBefore = false;
    bool pressed = false;
    bool released = false;
    for (const auto& source : *sources)
    {
        const auto state = sourceState(source);
        down = down || state.down;
        downBefore = downBefore || state.downBefore;
        pressed = pressed || state.pressed;
        released = released || state.released;
    }
    // Pressed: up as the frame started, and an input went down since. Released: up now, and an input went up.
    return { down, pressed && !downBefore, released && !down };
}

auto InputState::axis(std::string_view name) const -> float
{
    const auto* bindings = m_map.axis(name);
    if (!bindings)
    {
        warnUnknown("axis", name);
        return 0.0f;
    }

    float value = 0.0f;
    for (const auto& binding : *bindings)
    {
        const float bound = bindingValue(binding);
        if (std::abs(bound) > std::abs(value))
            value = bound;
    }
    return value;
}

auto InputState::sourceState(const InputSource& source) const -> SourceState
{
    const auto bits = [](const auto& down, const auto& before, const auto& pressed, const auto& released,
                          std::optional<std::size_t> i) -> SourceState {
        if (!i)
            return {};
        return { down[*i], before[*i], pressed[*i], released[*i] };
    };
    const auto edges = [](bool down, bool downBefore) -> SourceState {
        return { down, downBefore, down && !downBefore, !down && downBefore };
    };

    return core::match(
        source,
        [&](Key key) -> SourceState {
            if (m_keyboardCaptured)
                return {};
            return bits(m_keysDown, m_keysDownBefore, m_keysPressed, m_keysReleased, index<KEY_COUNT>(static_cast<int>(key)));
        },
        [&](MouseButton button) -> SourceState {
            if (m_mouseCaptured)
                return {};
            return bits(m_buttonsDown, m_buttonsDownBefore, m_buttonsPressed, m_buttonsReleased,
                index<BUTTON_COUNT>(static_cast<int>(button)));
        },
        [&](GamepadButton button) -> SourceState {
            if (m_keyboardCaptured)
                return {};
            return edges(gamepadBit(m_gamepad, button), gamepadBit(m_gamepadBefore, button));
        },
        [&](GamepadAxis axis) -> SourceState {
            if (m_keyboardCaptured)
                return {};
            return edges(gamepadValue(m_gamepad, axis) > AXIS_DOWN_THRESHOLD, gamepadValue(m_gamepadBefore, axis) > AXIS_DOWN_THRESHOLD);
        });
}

auto InputState::bindingValue(const AxisBinding& binding) const -> float
{
    return core::match(
        binding,
        [&](const AxisPair& pair) -> float {
            const float negative = sourceState(pair.negative).down ? 1.0f : 0.0f;
            const float positive = sourceState(pair.positive).down ? 1.0f : 0.0f;
            return positive - negative;
        },
        [&](GamepadAxis axis) -> float {
            const float value = std::clamp(gamepadAxis(axis), -1.0f, 1.0f);
            const float deadZone = std::clamp(m_map.deadZone(), 0.0f, 1.0f);
            const float magnitude = std::abs(value);
            if (magnitude <= deadZone)
                return 0.0f;
            return std::copysign((magnitude - deadZone) / (1.0f - deadZone), value);
        });
}

auto InputState::warnUnknown(std::string_view kind, std::string_view name) const -> void
{
    if (m_warnedNames.insert(std::format("{}:{}", kind, name)).second)
        log::warn("Input: no input {} named '{}'", kind, name);
}
}

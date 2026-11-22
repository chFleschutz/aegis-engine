export module aegis.platform:input_map;
import std;
import :key;

export namespace aegis::platform
{
/// @brief One input an input action or axis reads: a key, a mouse button, or a gamepad button or axis.
using InputSource = std::variant<Key, MouseButton, GamepadButton, GamepadAxis>;

/// @brief Two inputs that drive an input axis: -1 while 'negative' is down, 1 while 'positive' is, 0 for both or none.
struct AxisPair
{
    InputSource negative;
    InputSource positive;
};

/// @brief What an input axis reads: a pair of inputs, or a gamepad axis with the map's dead zone.
using AxisBinding = std::variant<AxisPair, GamepadAxis>;

/// @brief The input named 'name', as a project file writes it: the enum name of a key ("Space", "LeftShift"), a mouse
///        button with a "Mouse" prefix ("MouseLeft", "MouseButton4"), a gamepad button or axis with a "Gamepad" prefix
///        ("GamepadA", "GamepadLeftX"). None for any other name (also Key::Unknown, which has none).
[[nodiscard]] auto parseInputSource(std::string_view name) -> std::optional<InputSource>;

/// @brief The reverse of parseInputSource; empty for an input that has no name.
[[nodiscard]] auto inputSourceName(const InputSource& source) -> std::string_view;

/// @brief A project's input actions and axes by name, and the dead zone of its gamepad axes. A plain value: InputState
///        evaluates it; the project file fills it in.
class InputMap
{
public:
    static constexpr float DEFAULT_DEAD_ZONE{ 0.15f };

    /// @brief Adds the input action 'name', down while any of 'sources' is.
    /// @return False, and no change, if an action or axis already has the name.
    auto addAction(std::string name, std::vector<InputSource> sources) -> bool;

    /// @brief Adds the input axis 'name', whose value is that of the binding with the largest magnitude.
    /// @return False, and no change, if an action or axis already has the name.
    auto addAxis(std::string name, std::vector<AxisBinding> bindings) -> bool;

    /// @brief The inputs of the action 'name'; null if there is none.
    [[nodiscard]] auto action(std::string_view name) const -> const std::vector<InputSource>*;
    /// @brief The bindings of the axis 'name'; null if there is none.
    [[nodiscard]] auto axis(std::string_view name) const -> const std::vector<AxisBinding>*;

    /// @brief A gamepad axis closer to rest than this reads 0; the rest of its range is stretched to start at 0.
    [[nodiscard]] auto deadZone() const -> float { return m_deadZone; }
    auto setDeadZone(float deadZone) -> void { m_deadZone = deadZone; }

    [[nodiscard]] auto empty() const -> bool { return m_actions.empty() && m_axes.empty(); }

private:
    struct NameHash
    {
        using is_transparent = void;
        [[nodiscard]] auto operator()(std::string_view name) const noexcept -> std::size_t { return std::hash<std::string_view>{}(name); }
    };
    template<typename T>
    using NameMap = std::unordered_map<std::string, T, NameHash, std::equal_to<>>;

    [[nodiscard]] auto contains(std::string_view name) const -> bool;

    NameMap<std::vector<InputSource>> m_actions;
    NameMap<std::vector<AxisBinding>> m_axes;
    float m_deadZone{ DEFAULT_DEAD_ZONE };
};
}

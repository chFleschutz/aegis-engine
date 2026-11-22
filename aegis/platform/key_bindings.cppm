export module aegis.platform:key_bindings;
import std;
import :key;

export namespace aegis::platform
{
/// @brief Identifies one binding of a KeyBindings; never reused by that KeyBindings.
struct BindingId
{
    std::uint32_t value{ 0 };

    auto operator==(const BindingId&) const -> bool = default;
};

/// @brief Maps key events to callbacks. Plain logic with no window or GLFW behind it, so it runs in unit tests.
class KeyBindings
{
public:
    /// @brief Calls 'callback' whenever 'key' sees 'action' while exactly 'modifiers' are held.
    /// @return The id to unbind with. Dropping it keeps the binding for the life of this object.
    auto bind(Key key, std::function<void()> callback, KeyAction action = KeyAction::Press,
        Modifiers modifiers = Modifiers::None) -> BindingId;

    /// @brief Removes a binding, also while a dispatch is running: it then no longer runs, even for the
    ///        event being dispatched.
    /// @return False if 'id' is not bound (never was, or already removed).
    auto unbind(BindingId id) -> bool;

    /// @brief Runs, in binding order, the callbacks bound to this exact key, action and modifiers.
    /// @note Safe against callbacks that bind or unbind: bindings added during a dispatch first see the
    ///       next event, and the callbacks run from a copy, so a callback may even unbind itself.
    auto dispatch(Key key, KeyAction action, Modifiers modifiers) const -> void;

    [[nodiscard]] auto size() const -> std::size_t { return m_bindings.size(); }

private:
    struct Binding
    {
        BindingId id;
        Key key;
        KeyAction action;
        Modifiers modifiers;
        std::shared_ptr<const std::function<void()>> callback;
    };

    std::vector<Binding> m_bindings;
    std::uint32_t m_nextId{ 1 };
};
}

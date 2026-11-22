module aegis.platform;

import std;

namespace aegis::platform
{
auto KeyBindings::bind(Key key, std::function<void()> callback, KeyAction action, Modifiers modifiers) -> BindingId
{
    const BindingId id{ m_nextId++ };
    m_bindings.push_back({ id, key, action, modifiers, std::make_shared<const std::function<void()>>(std::move(callback)) });
    return id;
}

auto KeyBindings::unbind(BindingId id) -> bool
{
    return std::erase_if(m_bindings, [id](const Binding& binding) { return binding.id == id; }) > 0;
}

auto KeyBindings::dispatch(Key key, KeyAction action, Modifiers modifiers) const -> void
{
    // Callbacks may bind or unbind, which changes m_bindings: run from a copy of the matches.
    std::vector<std::pair<BindingId, std::shared_ptr<const std::function<void()>>>> matches;
    for (const auto& binding : m_bindings)
    {
        if (binding.key == key && binding.action == action && binding.modifiers == modifiers)
            matches.emplace_back(binding.id, binding.callback);
    }

    for (const auto& [id, callback] : matches)
    {
        // An earlier callback may have unbound this one; it must not run (it may capture a dead object).
        const auto stillBound = std::ranges::any_of(m_bindings, [id](const Binding& binding) { return binding.id == id; });
        if (stillBound && *callback)
            (*callback)();
    }
}
}

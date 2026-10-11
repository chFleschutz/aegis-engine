module;

#include "core/assert.h"

export module Aegis.Core.Input;

export import aegis.platform;

export namespace Aegis
{
/// @brief Old-stack access to the engine's aegis::platform::Input, until scripts and the editor are
///        ported to receive it explicitly. Only the Engine registers an instance.
class Input
{
public:
    using Key = aegis::platform::Key;
    using MouseButton = aegis::platform::MouseButton;
    using CursorMode = aegis::platform::CursorMode;

    [[nodiscard]] static auto instance() -> aegis::platform::Input&
    {
        AGX_ASSERT_X(s_input != nullptr, "Input instance is not registered");
        return *s_input;
    }

    static auto setInstance(aegis::platform::Input* input) -> void { s_input = input; }

private:
    inline static aegis::platform::Input* s_input{ nullptr };
};
}

#pragma once

/// Defines the one function the engine's launcher calls to find the game (see aegis::GameModule and
/// aegis_add_game). Use it once, in one source file of the game, after `import aegis.engine;`:
///
///     AEGIS_GAME_MODULE({ .name = "Template", .onStartup = onStartup })
///
/// A macro because the launcher's main() lives in the engine and the function is the game's; macros don't
/// cross module boundaries, so this is a header.
#define AEGIS_GAME_MODULE(...)                  \
    namespace aegis::launch                     \
    {                                           \
    auto gameModule() -> ::aegis::GameModule;   \
    auto gameModule() -> ::aegis::GameModule    \
    {                                           \
        return ::aegis::GameModule __VA_ARGS__; \
    }                                           \
    }

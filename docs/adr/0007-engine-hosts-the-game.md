# Game-usage model: the engine hosts the game

Formerly D7. Decided 2026-10-19, see
[`docs/editor-runtime-split.md`](../editor-runtime-split.md).

- **Executables:** Aegis owns `main()`. `aegis_add_game` builds one engine-owned launcher into
  `<Game>` (runtime) and `<Game>-Editor`, and only the editor executable links `aegis-editor`.
- **A game** is a C++ game module (`AEGIS_GAME_MODULE`, one registration function, statically
  linked) plus a project folder of data. A DLL/hot-reload build can come later through the same
  entry point.
- **Data:** scenes are files, loaded through a type registry that engine and game register into
  alike. Entities and assets are referenced by stable ID.
- **Scripting language:** planned for (scripts attached by name, state in registered fields, fixed
  hooks), not built.
- Why: the editor stays out of shipped games, a game needs no `main()` of its own, and scenes
  become savable and editable. Save games need the same serializer anyway.
- → `aegis.app` and the type registry ([#79](https://github.com/chFleschutz/aegis-engine/issues/79)–[#82](https://github.com/chFleschutz/aegis-engine/issues/82), [#129](https://github.com/chFleschutz/aegis-engine/issues/129), done), then [M9](https://github.com/chFleschutz/aegis-engine/milestone/10); export
  ([#134](https://github.com/chFleschutz/aegis-engine/issues/134)). Decides F1.

# Error types: one type per module, same shape

Formerly D1. Decided 2026-10-17.

Each module's `Error` is
`{ ErrorCode code; std::string message; std::optional<rhi::Error> rhi; }` (rhi keeps
`{code, debug location}`), built with `makeError(code, msg)` / `makeError(lowerError, msg)` and
printed only through the module's `describe(const Error&)`. Wrapping adds context and copies the
rhi cause; it never re-stringifies. Why: callers branch on module codes (`NotReady`, `OutOfDate`,
`FormatNotSupported`); the problem is lost causes, not the number of types. → 3.2
([#59](https://github.com/chFleschutz/aegis-engine/issues/59)–[#62](https://github.com/chFleschutz/aegis-engine/issues/62)).

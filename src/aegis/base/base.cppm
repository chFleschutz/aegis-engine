/// @brief Foundation module: the cross-cutting primitives every other module may use
///        (logging, flag enums, alignment, variant matching, the profiler).
/// @note Becomes `aegis.core`, now that the legacy core is gone (backlog decision D4).
export module aegis.base;

export import :align;
export import :flags;
export import :frame_clock;
export import :log;
export import :match;
export import :profiler;

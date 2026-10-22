/// @brief Foundation module: the cross-cutting primitives every other module may use
///        (logging, flag enums, alignment, variant matching, the profiler).
export module aegis.core;

export import :align;
export import :flags;
export import :frame_clock;
export import :log;
export import :match;
export import :profiler;

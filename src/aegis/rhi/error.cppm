export module aegis.rhi:error;
import std;
import :common;

export namespace aegis::rhi
{
struct Error
{
    ErrorCode code;
#ifndef NDEBUG
    std::source_location location;
#endif
};

// clang-format off: it can't lay out a parameter list split by #ifndef
[[nodiscard]] inline auto makeError(ErrorCode code
#ifndef NDEBUG
    ,
    std::source_location loc = std::source_location::current()
#endif
) noexcept -> std::unexpected<Error>
// clang-format on
{
    return std::unexpected{
        Error{
            .code = code,
#ifndef NDEBUG
            .location = std::move(loc)
#endif
        }
    };
}
}

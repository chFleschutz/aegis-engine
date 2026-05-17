module;
#include <expected>
#include <source_location>

export module aegis.rhi:error;
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

[[nodiscard]] inline auto makeError(ErrorCode code
#ifndef NDEBUG
    ,
    std::source_location loc = std::source_location::current()
#endif
) noexcept -> std::unexpected<Error>
{
    return std::unexpected{
        Error{
            .code = code,
#ifndef NDEBUG
            .location = loc
#endif
        }
    };
}
}

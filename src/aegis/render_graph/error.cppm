module;
#include <expected>
#include <optional>
#include <string>
#include <utility>

export module aegis.render_graph:error;
import aegis.rhi;

export namespace aegis::render_graph
{
struct Error
{
    std::string message;
    std::optional<rhi::Error> rhi; // Set when an rhi call failed
};

[[nodiscard]] inline auto makeError(std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .message = std::move(message) } };
}

[[nodiscard]] inline auto makeError(const rhi::Error& error, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .message = std::move(message), .rhi = error } };
}
}

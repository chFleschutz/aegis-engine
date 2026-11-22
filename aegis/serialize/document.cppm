export module aegis.serialize:document;
import std;
import aegis.reflect;
import :error;

// What every native file shares: the `{type, id, version}` header, IDs as 16 lowercase hex digits, and the
// checks that turn a node of the wrong shape into an error at its line. Module-internal: the file formats
// (:scene, :material) build on it.
namespace aegis::serialize::detail
{
// The header every native file starts with.
struct Header
{
    std::uint64_t id{ 0 };
    std::uint32_t version{ 0 };
};

// Checks that `root` is an object of type `type` with only the keys `keys`, and reads its ID and version.
// Fails with WrongType for another type and UnsupportedVersion for a version above `current`.
[[nodiscard]] auto readHeader(const reflect::Node& root, std::string_view type, std::uint32_t current, std::span<const std::string_view> keys)
    -> std::expected<Header, Error>;

// A new object node with the header members.
[[nodiscard]] auto writeHeader(std::string_view type, std::uint64_t id, std::uint32_t version) -> reflect::Node;

[[nodiscard]] auto formatId(std::uint64_t id) -> std::string;

// An ID as files write it: exactly 16 lowercase hex digits, and not zero.
[[nodiscard]] auto parseId(const reflect::Node& node, std::string_view what) -> std::expected<std::uint64_t, Error>;

// A whole number of at least 1.
[[nodiscard]] auto parseVersion(const reflect::Node& node, std::string_view what) -> std::expected<std::uint32_t, Error>;

[[nodiscard]] auto expectKind(const reflect::Node& node, reflect::Node::Kind kind, std::string_view what) -> std::expected<void, Error>;

// The member `key` of `object`, which must be there and of kind `kind`.
[[nodiscard]] auto required(const reflect::Node& object, std::string_view key, reflect::Node::Kind kind, std::string_view what)
    -> std::expected<const reflect::Node*, Error>;

// The member `key` of `object` if it is there (null if not); it must be of kind `kind`.
[[nodiscard]] auto optional(const reflect::Node& object, std::string_view key, reflect::Node::Kind kind)
    -> std::expected<const reflect::Node*, Error>;

// Fails with InvalidData at the first member of `object` whose key is not in `known`.
[[nodiscard]] auto checkKeys(const reflect::Node& object, std::span<const std::string_view> known, std::string_view what)
    -> std::expected<void, Error>;
}

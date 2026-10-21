// aegis.serialize: native files (scenes) through the type registry.
//
// The serializer knows no component: it reads and writes whatever the reflect::TypeRegistry describes, so a
// game's own types save like the built-in ones. Files are JSON (docs/asset-system.md, "Formats and
// libraries"), chosen in one place (:json).
//
// Layers (each its own partition):
//   :error  Error / ErrorCode / makeError / describe. Errors about a file carry its line.
//   :json   reflect::Node <-> JSON text: JSONC on read, one fixed layout on write.
//   :scene  Scene files: read and check them, write them, save a world, instantiate a scene into a world.
export module aegis.serialize;

export import :error;
export import :json;
export import :scene;

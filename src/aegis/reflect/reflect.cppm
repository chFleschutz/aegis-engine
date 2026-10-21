// aegis.reflect: the type registry.
//
// A runtime description of every type that can be saved, shown in an editor or scripted. A type is
// registered by an explicit, stable name (the name is the save-file format, so it is never derived
// from the C++ class name), followed by one `.field(...)` call per persistent member:
//
//     aegis::reflect::TypeRegistry types;
//     types.component<PointLight>("PointLight")
//         .field("color", &PointLight::color)
//         .field("intensity", &PointLight::intensity);
//
// Built-in components and a game's own types register through this same API.
//
// Saved forms change over time, so each type has its own version (1 until it is raised) and one
// migration per step, which upgrades the saved fields from version n to n + 1 on load. A renamed type
// or field keeps its old name as an alias, so files written before the rename still load:
//
//     types.component<PointLight>("PointLight")
//         .field("color", &PointLight::color)
//         .field("intensity", &PointLight::intensity)
//         .version(2)
//         .migration(1, [](Node& fields) { ...; return std::expected<void, Error>{}; })
//         .alias("OmniLight")
//         .fieldAlias("power", "intensity");
//
// Layers (each its own partition):
//   :error       Error / ErrorCode / makeError / describe.
//   :value       FieldKind, the type-erased Value, and its text form (toText / fromText).
//   :node        Node, a JSON-shaped tree of values (what serializers and migrations work on), and a
//                field value's node form (toNode / fromNode).
//   :conversion  FieldConversion<T>, the per-type trait that maps a C++ member type to a Value, and
//                EnumNames<E>, the opt-in name table that makes an enum a field type.
//   :type_info   FieldInfo and TypeInfo: the immutable description of one registered type.
//   :registry    TypeRegistry and TypeBuilder<T>: registration and lookup by name or C++ type.
//
// Not built yet, but the shape leaves room: TypeInfo holds fields only, and a `functions` list (name,
// parameter and result FieldKinds, a Value-based invoker) can sit beside it for a scripting VM without
// changing registration or lookup. Automatic field lists (Boost.PFR, reflect-cpp, C++26 reflection)
// would call TypeBuilder::field in a loop; nothing here assumes the list is hand-written.
//
// This module depends only on aegis.math (for glm field types): not on scene, EnTT or rhi.
export module aegis.reflect;

export import :conversion;
export import :error;
export import :node;
export import :registry;
export import :type_info;
export import :value;

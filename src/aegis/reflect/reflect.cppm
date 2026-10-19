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
// Layers (each its own partition):
//   :error       Error / ErrorCode / makeError / describe.
//   :value       FieldKind, the type-erased Value, and its text form (toText / fromText).
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
export import :registry;
export import :type_info;
export import :value;

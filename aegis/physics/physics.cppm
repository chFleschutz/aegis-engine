// aegis.physics: rigid bodies through Jolt Physics. A PhysicsSystem per world simulates the entities with a Collider
// (and a RigidBody, or none for a static body) in fixed steps while the world plays. Jolt's headers are included only
// in this module's .cpp files.
//
// Partitions:
//   :components      RigidBody, Collider and its shapes, and registerComponents.
//   :queries         Ray casts and overlap tests against a playing world's bodies.
//   :contacts        ContactBegin and ContactEnd, the events of bodies that touch.
//   :physics_system  PhysicsSystem: the world's Jolt physics, its bodies and the poses written back.
export module aegis.physics;

export import :components;
export import :queries;
export import :contacts;
export import :physics_system;

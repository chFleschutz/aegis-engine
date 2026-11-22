// aegis.scripting: behaviour attached to entities, and the system that runs it.
//
// Partitions:
//   :script                        Script, its ScriptContext, the Scripts component, registerScript and attachScript.
//   :script_system                 ScriptSystem: runs the scripts' hooks.
//   :kinematic_movement_controller KinematicMovementController: the fly-camera script.
export module aegis.scripting;

export import :kinematic_movement_controller;
export import :script;
export import :script_system;

// aegis.game: what a game's code uses, in one import.
//
// The engine as a game sees it (Engine, GameModule), the world and its components, scripts, physics, reflection, the
// asset library and assets, scene descriptions and their defaults, math and logging. A game's sources write
// `import aegis.game;`; the launcher, the editor and the tests import the modules they use themselves.
export module aegis.game;

export import aegis.asset_library;
export import aegis.assets;
export import aegis.core;
export import aegis.defaults;
export import aegis.engine;
export import aegis.math;
export import aegis.physics;
export import aegis.reflect;
export import aegis.scene_description;
export import aegis.scripting;
export import aegis.world;

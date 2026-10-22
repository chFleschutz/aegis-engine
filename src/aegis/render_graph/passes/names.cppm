export module aegis.render_passes:names;
import std;

/// The names of the graph resources more than one pass declares: one pass creates the resource, the others
/// reference it by name. Using the constant instead of a literal turns a typo into a compile error.
export namespace aegis::render_graph::names
{
// G-buffer, created by the geometry pass
inline constexpr std::string_view position{ "Position" };
inline constexpr std::string_view normal{ "Normal" };
inline constexpr std::string_view albedo{ "Albedo" };
inline constexpr std::string_view arm{ "ARM" }; // Ambient occlusion, roughness, metallic
inline constexpr std::string_view emissive{ "Emissive" };
inline constexpr std::string_view depth{ "Depth" };

// Scene data the CPU writes every frame
inline constexpr std::string_view cameraData{ "CameraData" };
inline constexpr std::string_view lightingUniforms{ "LightingUniforms" };

// GPU-driven path
inline constexpr std::string_view staticInstances{ "StaticInstances" };
inline constexpr std::string_view dynamicInstances{ "DynamicInstances" };
inline constexpr std::string_view drawBatches{ "DrawBatches" };
inline constexpr std::string_view visibleInstances{ "VisibleInstances" };
inline constexpr std::string_view indirectDrawCommands{ "IndirectDrawCommands" };
inline constexpr std::string_view indirectDrawCounts{ "IndirectDrawCounts" };

// Lit and post-processed images
inline constexpr std::string_view sceneColor{ "SceneColor" };
inline constexpr std::string_view bloom{ "Bloom" };
inline constexpr std::string_view finalImage{ "Final" };
}

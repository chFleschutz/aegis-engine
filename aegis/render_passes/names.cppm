export module aegis.render_passes:names;
import std;
import aegis.render_graph;

/// The names of the graph resources more than one pass declares: one pass creates the resource, the others
/// reference it by name. Using the constant instead of a literal turns a typo into a compile error. A view's scene
/// passes declare them in the view's scope, where they become "<view>/<name>".
export namespace aegis::render_graph::names
{
// The renderer's target for the frame, imported by the first pass that declares it (declareFrameTarget) and bound by
// FrameGraph::execute.
inline constexpr std::string_view target{ frameTarget };

// The view the world renders through when the shell asks for none: the main camera, presented. Its resources are
// scoped to it like any view's ("Main/Depth", see scopedName).
inline constexpr std::string_view mainView{ "Main" };

// G-buffer, created by the geometry pass
inline constexpr std::string_view position{ "Position" };
inline constexpr std::string_view normal{ "Normal" };
inline constexpr std::string_view albedo{ "Albedo" };
inline constexpr std::string_view arm{ "ARM" }; // Ambient occlusion, roughness, metallic
inline constexpr std::string_view emissive{ "Emissive" };
inline constexpr std::string_view depth{ "Depth" };
inline constexpr std::string_view spotShadowAtlas{ "SpotShadowAtlas" }; // Unscoped: every spot shadow view's tile

// Scene data the CPU writes every frame
inline constexpr std::string_view cameraData{ "CameraData" };
inline constexpr std::string_view lightingUniforms{ "LightingUniforms" };
inline constexpr std::string_view lights{ "Lights" }; // The point and spot lights, LightData

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

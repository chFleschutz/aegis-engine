module;
#include <cassert>

export module aegis.render_passes:frame_scene;
import std;
import aegis.math;
import aegis.rhi;
import :instance_store;

export namespace aegis::render_graph
{
/// @brief post_process.slang's tone mapping modes (the 'toneMappingMode' switch).
enum class ToneMapping : std::int32_t
{
    Reinhard = 0,
    Aces = 1,
};

/// @brief What a view is for. Each kind renders with its own passes (addScenePasses).
enum class ViewKind
{
    Main,   // The scene through a camera, lit and post-processed into its "Final"
    Shadow, // The depth of the scene seen from a light, into its "Depth" (or its tile of "SpotShadowAtlas"): a shadow map
            // a main view's lighting samples
};

/// @brief What the passes draw of the world in one frame: the instances, the views (each a camera), the lights, the
///        sky's environment and the post-processing settings. Cameras, lights and environment are copied out of the
///        world once per frame (aegis::extractFrameScene); the instances are not copied, the store follows the world
///        as it changes (aegis::InstanceBridge). Passes read it instead of the ECS, and tests build one by hand.
struct FrameScene
{
    /// @brief A camera with this frame's matrices.
    struct Camera
    {
        math::Float4x4 view = math::Float4x4::identity();
        math::Float4x4 projection = math::Float4x4::identity();
        math::Float3 position{ 0.0f };
    };

    /// @brief One rendering of the world in the frame: its scene passes run in a frame graph scope of its name, with
    ///        resources of their own (DefaultPassesDesc::view picks the view by its index in 'views').
    struct View
    {
        std::string name;
        /// @brief None: the world has no camera to render through, and the view's passes draw nothing.
        std::optional<Camera> camera;
        /// @brief The extent its textures have; none: the output's (the window, or the offscreen target).
        std::optional<rhi::Extent2D> extent;
        ViewKind kind{ ViewKind::Main };
        /// @brief A main view's: the index in 'views' of the first of the directional light's shadow views (its
        ///        cascades, maxShadowCascades of them one after another) that its lighting samples (addShadowViews);
        ///        none: it is lit unshadowed.
        std::optional<std::uint32_t> shadow;
        /// @brief A spot shadow view's: the tile of "SpotShadowAtlas" it draws into (spotShadowTile), in texels; none:
        ///        it draws into its own "Depth".
        std::optional<rhi::Rect2D> tile;
    };

    struct AmbientLight
    {
        math::Float3 color{ 1.0f };
        float intensity{ 1.0f };
    };

    struct DirectionalLight
    {
        math::Float3 color{ 1.0f };
        float intensity{ 1.0f };
        math::Float3 direction{ 0.0f, 0.0f, 1.0f }; // World space, normalized: towards the light
        float shadowDistance{ 50.0f };              // Meters from the camera its shadow covers (shadowCascadeSplits)
        float shadowBias{ 1.0f };                   // Shadow-map texels a shaded point moves along its normal
        std::uint32_t shadowCascades{ 4 };          // Shadow maps the distance is split into, 1 to maxShadowCascades
        float shadowSplitLambda{ 0.75f };           // 1: logarithmic splits, 0: uniform (shadowCascadeSplits)
    };

    struct PointLight
    {
        math::Float3 position{ 0.0f };
        math::Float3 color{ 1.0f };
        float intensity{ 1.0f };
        float range{ 20.0f }; // Meters; the light fades to zero there
        float scale{ 1.0f };  // The length of the transform's x axis, which sizes the light's billboard
    };

    struct SpotLight
    {
        math::Float3 position{ 0.0f };
        math::Float3 direction{ 0.0f, 0.0f, 1.0f }; // World space, normalized: where the cone points
        math::Float3 color{ 1.0f };
        float intensity{ 1.0f };
        float range{ 20.0f };                         // Meters; the light fades to zero there
        float innerConeAngle{ 0.0f };                 // Radians from the axis where the light starts to fade
        float outerConeAngle{ math::radians(45.0f) }; // Radians from the axis where it reaches zero
        bool castsShadows{ false };                   // Whether it competes for a spot shadow view (selectShadowedSpotLights)
        float shadowBias{ 1.0f };                     // Shadow-map texels a shaded point moves along its normal
        /// @brief The index in 'views' of the spot shadow view it casts its shadow through (addShadowViews); none: it
        ///        is unshadowed this frame.
        std::optional<std::uint32_t> shadowView;
    };

    /// @brief The sky's image-based lighting; a texture the environment lacks is empty.
    struct Environment
    {
        std::optional<rhi::TextureDescriptor> irradiance;
        std::optional<rhi::TextureDescriptor> prefiltered;
        std::optional<rhi::TextureDescriptor> skybox;
    };

    /// @brief How the frame is finished: tone mapping, exposure, gamma and bloom (BloomPass, PostProcessingPass).
    ///        The defaults are the settings of a camera without an aegis::PostProcess.
    struct PostProcess
    {
        ToneMapping toneMapping{ ToneMapping::Aces };
        float exposure{ 1.0f };
        float gamma{ 2.2f };
        float bloomIntensity{ 0.1f };
        float bloomThreshold{ 0.0f };
        float bloomSoftThreshold{ 0.5f };
    };

    const InstanceStore& instances;
    /// @brief In the order of the frame graph's view scopes (DefaultPassesDesc::views).
    std::vector<View> views;
    std::optional<AmbientLight> ambientLight;
    std::optional<DirectionalLight> directionalLight;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;
    Environment environment;
    PostProcess postProcess;

    /// @brief The camera of the view at 'view' in views; none without one, or past the end.
    [[nodiscard]] auto camera(std::uint32_t view) const -> std::optional<Camera>
    {
        return view < views.size() ? views[view].camera : std::nullopt;
    }
};

/// @brief Where the passes that read the frame scene find it. Whoever builds the graph owns one, hands it to those
///        passes (DefaultPassesDesc::scene) and points it at each frame's scene (bind) around FrameGraph::prepare
///        and execute; the frame graph itself never sees the scene.
class FrameSceneSource
{
public:
    /// @brief Keeps the source pointed at a scene until it ends, then points it back where it was.
    class [[nodiscard]] Scope
    {
    public:
        Scope(const Scope&) = delete;
        Scope(Scope&&) = delete;
        ~Scope() { m_source.m_current = m_previous; }

        auto operator=(const Scope&) -> Scope& = delete;
        auto operator=(Scope&&) -> Scope& = delete;

    private:
        friend class FrameSceneSource;

        Scope(FrameSceneSource& source, const FrameScene& scene) :
            m_source{ source },
            m_previous{ source.m_current }
        {
            m_source.m_current = &scene;
        }

        FrameSceneSource& m_source;
        const FrameScene* m_previous;
    };

    FrameSceneSource() = default;
    FrameSceneSource(const FrameSceneSource&) = delete;
    FrameSceneSource(FrameSceneSource&&) = delete;
    ~FrameSceneSource() = default;

    auto operator=(const FrameSceneSource&) -> FrameSceneSource& = delete;
    auto operator=(FrameSceneSource&&) -> FrameSceneSource& = delete;

    /// @brief Points the source at 'scene' for as long as the returned scope lives.
    [[nodiscard]] auto bind(const FrameScene& scene) -> Scope { return Scope{ *this, scene }; }
    auto bind(const FrameScene&& scene) -> Scope = delete;

    /// @brief Whether a scene is bound.
    [[nodiscard]] auto bound() const noexcept -> bool { return m_current != nullptr; }
    /// @brief The bound scene. Only while one is bound: in FrameGraph::prepare and execute.
    [[nodiscard]] auto current() const -> const FrameScene&
    {
        assert(m_current && "No frame scene is bound: bind one around FrameGraph::prepare and execute");
        return *m_current;
    }

private:
    const FrameScene* m_current{ nullptr };
};
}

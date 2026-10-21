export module aegis.render_graph:frame_scene;
import std;
import aegis.math;
import aegis.rhi;
import :instance_store;

export namespace aegis::render_graph
{
/// @brief What the passes draw of the world in one frame: the instances, the main camera, the lights and the
///        sky's environment. Camera, lights and environment are copied out of the world once per frame
///        (aegis::extractFrameScene); the instances are not copied, the store follows the world as it changes
///        (aegis::InstanceBridge). Passes read it instead of the ECS, and tests build one by hand.
struct FrameScene
{
    /// @brief The main camera, with this frame's matrices.
    struct Camera
    {
        glm::mat4 view{ 1.0f };
        glm::mat4 projection{ 1.0f };
        glm::vec3 position{ 0.0f };
    };

    struct AmbientLight
    {
        glm::vec3 color{ 1.0f };
        float intensity{ 1.0f };
    };

    struct DirectionalLight
    {
        glm::vec3 color{ 1.0f };
        float intensity{ 1.0f };
        glm::vec3 direction{ 0.0f, 0.0f, 1.0f }; // World space, normalized
    };

    struct PointLight
    {
        glm::vec3 position{ 0.0f };
        glm::vec3 color{ 1.0f };
        float intensity{ 1.0f };
        float scale{ 1.0f }; // The transform's x scale, which sizes the light's billboard
    };

    /// @brief The sky's image-based lighting; a texture the environment lacks is empty.
    struct Environment
    {
        std::optional<rhi::TextureDescriptor> irradiance;
        std::optional<rhi::TextureDescriptor> prefiltered;
        std::optional<rhi::TextureDescriptor> skybox;
    };

    const InstanceStore& instances;
    std::optional<Camera> camera;
    std::optional<AmbientLight> ambientLight;
    std::optional<DirectionalLight> directionalLight;
    std::vector<PointLight> pointLights; // All of them; LightingPass shades the first maxPointLights
    Environment environment;
};
}

module;

#include <imgui.h>
#include <stb/stb_image.h>

module aegis.test;

import std;
import aegis.assets;
import aegis.engine;
import aegis.render_graph;
import aegis.render_passes;
import aegis.rhi;
import aegis.world;

namespace aegis::test
{
namespace
{
// What the shell found, read once the engine (and the shell with it) is gone.
struct SceneResult
{
    std::vector<std::string> errors;
    rhi::Extent2D view{ viewExtent }; // --view: the view's size in the last frame
    std::uint32_t viewShown{ 0 };     // --view: frames whose UI showed the view (Presentation::uiTexture)
};

// --views: the views, by name; the main one renders through the main camera.
constexpr std::string_view mainView{ "Main" };
constexpr std::string_view sideView{ "Side" };

// The static entities of 'registry' a geometry pass draws: those with a mesh and a material.
auto drawableStatic(world::Registry& registry) -> std::uint32_t
{
    std::uint32_t count{ 0 };
    for (auto&& [entity, renderer] : registry.view<MeshRenderer>().each())
    {
        if (renderer.mesh && renderer.material && !registry.has<DynamicTag>(world::Entity{ entity }))
            ++count;
    }
    return count;
}

// The static instances the instance pass wrote in the last frame; none on the CPU-driven path, which has no such pass.
auto writtenStatic(const render_graph::FrameGraph& frameGraph) -> std::optional<std::uint32_t>
{
    for (const auto* pass : frameGraph.passes())
    {
        if (const auto* instances = dynamic_cast<const render_graph::InstanceUpdatePass*>(pass))
            return instances->staticCount();
    }
    return std::nullopt;
}

// Whether the G-buffer pass of 'frameGraph' holds the G-buffer pipeline of 'variant': GpuDrivenGeometryPass on the
// GPU-driven path, GeometryPass's StaticMeshRenderSystem (counter-clockwise front faces) on the CPU-driven one.
auto hasGeometryPipeline(const render_graph::FrameGraph& frameGraph, const assets::MaterialVariant& variant) -> bool
{
    using render_graph::GeometryPath;
    using render_graph::Technique;
    for (const auto* pass : frameGraph.passes())
    {
        // A shadow view's geometry pass draws depth only.
        const auto* gpuDriven = dynamic_cast<const render_graph::GpuDrivenGeometryPass*>(pass);
        if (gpuDriven && gpuDriven->technique() == Technique::GBuffer)
            return gpuDriven->pipelines().contains({ .technique = Technique::GBuffer, .variant = variant, .path = GeometryPath::GpuDriven });
        const auto* geometry = dynamic_cast<const render_graph::GeometryPass*>(pass);
        if (geometry && geometry->technique() == Technique::GBuffer)
        {
            for (const auto& system : geometry->renderSystems())
            {
                if (const auto* meshes = dynamic_cast<const render_graph::StaticMeshRenderSystem*>(system.get()))
                    return meshes->pipelines().contains({ .technique = Technique::GBuffer, .variant = variant, .path = GeometryPath::CpuDriven });
            }
        }
    }
    return false;
}

// Whether the forward pass of 'frameGraph' holds the forward pipelines of 'variant', for both front faces (mirrored
// instances draw with the clockwise one): ForwardPass's StaticMeshRenderSystem, on both geometry paths.
auto hasForwardPipeline(const render_graph::FrameGraph& frameGraph, const assets::MaterialVariant& variant) -> bool
{
    using render_graph::GeometryPath;
    using render_graph::Technique;
    for (const auto* pass : frameGraph.passes())
    {
        const auto* forward = dynamic_cast<const render_graph::ForwardPass*>(pass);
        if (!forward)
            continue;
        for (const auto& system : forward->renderSystems())
        {
            const auto* meshes = dynamic_cast<const render_graph::StaticMeshRenderSystem*>(system.get());
            if (!meshes || meshes->technique() != Technique::Forward)
                continue;
            return std::ranges::all_of(std::array{ rhi::FrontFace::CounterClockwise, rhi::FrontFace::Clockwise }, [&](rhi::FrontFace frontFace) {
                return meshes->pipelines().contains({ .technique = Technique::Forward, .variant = variant, .path = GeometryPath::CpuDriven, .frontFace = frontFace });
            });
        }
    }
    return false;
}

// 'root' and its descendants.
auto subtree(world::Registry& registry, world::Entity root) -> std::vector<world::Entity>
{
    std::vector<world::Entity> entities{ root };
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        for (auto child = registry.get<Children>(entities[i]).first; child;
            child = registry.get<Siblings>(child).next)
            entities.push_back(child);
    }
    return entities;
}

// Destroys 'root' and its descendants.
void destroySubtree(world::Registry& registry, world::Entity root)
{
    for (const auto entity : subtree(registry, root))
        registry.destroy(entity);
}

// --unlit: the unlit cube's colour. Pure channels survive tone mapping and gamma exactly where they are 0, so any
// light on the cube shows as red (verifyUnlit).
constexpr glm::vec3 unlitColor{ 0.0f, 1.0f, 1.0f };

// --post-process: settings far enough from the defaults that a frame rendered with the defaults fails the reference.
constexpr PostProcess postProcessSettings{
    .toneMapping = ToneMapping::Reinhard,
    .exposure = 1.8f,
    .bloomIntensity = 0.5f,
    .bloomThreshold = 0.5f,
};

// --views: the pixels of 'pixels' (RGBA8) that show --unlit's cube colour unlit (see verifyUnlit).
auto unlitPixels(std::span<const std::byte> pixels) -> std::size_t
{
    std::size_t count{ 0 };
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        const auto red = std::to_integer<int>(pixels[i]);
        const auto green = std::to_integer<int>(pixels[i + 1]);
        const auto blue = std::to_integer<int>(pixels[i + 2]);
        if (red == 0 && green >= 184 && green <= 190 && blue >= 184 && blue <= 190)
            ++count;
    }
    return count;
}

// The test's side of the app: edits the loaded scene at the safe point (--mirror, --mutate-scene, --post-process), renders it
// through a second camera into a view of its own (--view) or next to the main camera's (--views), renders a second world for a while (--worlds), draws the
// stats window and checks the frame graph's timings. Failures go to the SceneResult; the run carries on.
class SceneShell final : public Shell
{
public:
    SceneShell(const Options& options, SceneResult& result) :
        m_options{ options },
        m_result{ result }
    {
    }

    auto onStartup(Engine& engine) -> std::expected<void, EngineError> override
    {
        m_engine = &engine;
        return {};
    }

    void onFrame(FrameContext& frame) override
    {
        auto& registry = frame.world.registry();
        if (m_frame == 0)
            setUp(frame.world);
        if (m_options.unlit && m_frame == 1)
        {
            if (auto added = addUnlitCubes(frame); !added)
                m_result.errors.push_back(std::format("Adding the unlit cubes failed: {}", added.error()));
        }
        if (m_options.doubleSided && m_frame == 1)
        {
            if (auto added = addBackFacingPlane(frame); !added)
                m_result.errors.push_back(std::format("Adding the double-sided plane failed: {}", added.error()));
        }
        if (m_options.blend && m_frame == 1)
        {
            if (auto added = addBlendedCubes(frame); !added)
                m_result.errors.push_back(std::format("Adding the blended cubes failed: {}", added.error()));
        }
        if (m_options.blend && m_options.frames != 0 && m_frame + 1 == m_options.frames)
        {
            if (auto verified = verifyBlended(frame); !verified)
                m_result.errors.push_back(std::format("Blended material check failed: {}", verified.error()));
        }
        if (m_options.views && m_frame == 1)
        {
            if (auto added = addSideCube(frame); !added)
                m_result.errors.push_back(std::format("Adding the side view's cube failed: {}", added.error()));
        }
        // --views compares frames rendered apart: the world stands still once it is set up.
        if (!m_options.views || m_frame < viewsFreezeFrame)
            frame.update(frame.world, world::UpdateMode::Play);
        if (m_options.views)
        {
            registry.get<CameraMatrices>(m_viewCamera).aspect = static_cast<float>(sideViewExtent.x) / static_cast<float>(sideViewExtent.y);
            if (auto checked = checkViews(); !checked)
                m_result.errors.push_back(std::format("Views check failed: {}", checked.error()));
        }

        if (m_options.mutateScene)
        {
            if (auto mutated = mutateScene(frame); !mutated)
                m_result.errors.push_back(std::format("Scene change failed: {}", mutated.error()));
        }
        if (m_options.worlds)
        {
            if (auto switched = switchWorlds(frame); !switched)
                m_result.errors.push_back(std::format("World switch failed: {}", switched.error()));
        }

        if (m_options.view)
        {
            // Headless runs resize the view halfway, so the graph's resolution-dependent textures and the views
            // passes derive from them are recreated under validation.
            if (m_options.frames != 0 && m_frame == m_options.frames / 2)
                m_result.view = resizedViewExtent;
            const auto extent = m_result.view;
            registry.get<CameraMatrices>(m_viewCamera).aspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);
        }

        buildUi(frame.frameGraph);
        if (m_options.view)
            showView(frame);
        if (m_options.frames != 0 && m_frame + 1 == m_options.frames)
            verifyTimings(frame.frameGraph);
        ++m_frame;
    }

    // The view's camera replaces the main one once the update has given it its matrices (from the second frame).
    [[nodiscard]] auto renderViews() const -> std::vector<RenderView> override
    {
        // --post-process: through the main camera into a view, then onto the target, so the frame graph is rebuilt
        // halfway and the last frame shows whether the settings survived it.
        if (m_options.postProcess)
        {
            if (m_options.frames != 0 && m_frame < m_options.frames / 2)
                return { RenderView{ .extent = sceneExtent } };
            return {};
        }
        if (m_options.views)
        {
            // Both views, or one of them alone while checkViews renders it apart.
            std::vector<RenderView> views;
            if (m_viewsShown != ViewsShown::Side)
                views.push_back({ .name = std::string{ mainView }, .extent = viewExtent });
            if (m_viewsShown != ViewsShown::Main)
                views.push_back({ .name = std::string{ sideView }, .camera = viewCamera(), .extent = sideViewExtent });
            return views;
        }
        if (!m_options.view)
            return {};
        return { RenderView{ .camera = viewCamera(), .extent = m_result.view } };
    }

private:
    // The first frame: the edits the options ask for on the scene as the file left it.
    void setUp(world::World& world)
    {
        m_world = &world;
        auto& registry = world.registry();
        for (auto&& [entity, name] : registry.view<Name>().each())
        {
            if (name.name == "DamagedHelmet")
                m_helmet = world::Entity{ entity };
        }
        // Another scene file (--scene-file) has no helmet, and no option that edits it runs with one.
        if (!m_helmet && m_options.sceneFile == testSceneFile)
            m_result.errors.push_back(std::format("{} has no entity named DamagedHelmet", m_options.sceneFile));
        else if (m_options.mirror)
        {
            // A scale of -1 on x must not turn the helmet inside out.
            registry.get<Transform>(m_helmet).scale.x = -1.0f;
            registry.markStaticDirty(m_helmet);
        }

        if (m_options.postProcess)
        {
            if (const auto camera = world.mainCamera())
                registry.add<PostProcess>(camera, postProcessSettings);
            else
                m_result.errors.push_back(std::format("{} has no main camera", m_options.sceneFile));
        }

        if (m_options.view || m_options.views)
        {
            // Ahead and to the right of the main camera, turned towards the helmet; CameraSystem keeps its matrices.
            m_viewCamera = registry.create("View Camera", glm::vec3{ 2.2f, -2.2f, 0.3f }, math::quatFromEuler(glm::radians(glm::vec3{ 0.0f, 0.0f, 45.0f })));
            registry.add<Camera>(m_viewCamera);
            registry.markStaticDirty(m_viewCamera); // Once the world has begun, a new static entity needs it for its global transform
        }
    }

    // --views: the views checkViews renders, together or one alone.
    enum class ViewsShown
    {
        Both,
        Main,
        Side,
    };

    // --views: the frame from which the world no longer updates, so each view renders the same scene together and alone.
    static constexpr std::uint32_t viewsFreezeFrame{ 10 };

    // --view, --views: the second camera, once the update has given it its matrices (from the second frame).
    [[nodiscard]] auto viewCamera() const -> std::optional<RenderView::Camera>
    {
        if (m_frame <= 1)
            return std::nullopt;
        auto& registry = m_world->registry(); // Registry::get has no const overload
        const auto& view = registry.get<CameraMatrices>(m_viewCamera);
        return RenderView::Camera{
            .view = view.view,
            .projection = view.projection,
            .position = registry.get<GlobalTransform>(m_viewCamera).location,
        };
    }

    // --views, in the second frame: an unlit cube just ahead of the second camera, outside the main camera's frustum,
    // so culling must keep it in the side view and drop it from the main one. The main camera's PostProcess is
    // --unlit's, so its colour reaches the image as unlitPixels expects.
    auto addSideCube(FrameContext& frame) -> std::expected<void, std::string>
    {
        auto& registry = frame.world.registry();
        const auto& camera = registry.get<GlobalTransform>(m_viewCamera);
        auto unlitCreated = frame.assets.createMaterial(frame.assets.defaults().unlitTemplate);
        if (!unlitCreated)
            return std::unexpected{ describe(unlitCreated.error()) };
        auto& unlit = *unlitCreated;
        if (auto set = unlit->setParameter("color", unlitColor); !set)
            return std::unexpected{ assets::describe(set.error()) };
        auto cube = frame.assets.instantiateScene(frame.world, frame.types, assets::engine::cube, {});
        if (!cube)
            return std::unexpected{ describe(cube.error()) };
        auto& transform = registry.get<Transform>(*cube);
        transform.location = camera.location + camera.forward() * 0.5f;
        transform.rotation = camera.rotation;
        transform.scale = glm::vec3{ 0.1f };
        for (const auto entity : subtree(registry, *cube))
        {
            if (registry.has<MeshRenderer>(entity))
                registry.get<MeshRenderer>(entity).material = unlit;
            registry.markStaticDirty(entity);
        }

        return showUnlitColor(frame.world);
    }

    // --views, at the safe point. Frame 40 reads both views rendered together in the frame before (the world stood
    // still since viewsFreezeFrame): the side cube shows in the side view only. Then each view renders alone (the frame
    // graph rebuilt for it), and its image, read the frame after, must equal the one rendered together, pixel for
    // pixel. From frame 45 on both render again, for the screenshots.
    auto checkViews() -> std::expected<void, std::string>
    {
        constexpr std::uint32_t togetherFrame{ 40 };
        const auto read = [&](std::string_view view) -> std::expected<std::vector<std::byte>, std::string> {
            auto pixels = m_engine->readView(view);
            if (!pixels)
                return std::unexpected{ std::format("reading the view '{}' back failed: {}", view, pixels.error()) };
            return pixels;
        };
        const auto alone = [&](std::string_view view, ViewsShown next) -> std::expected<void, std::string> {
            m_viewsShown = next;
            const auto pixels = read(view);
            if (!pixels)
                return std::unexpected{ pixels.error() };
            const auto& together = m_together.find(view)->second;
            if (pixels->size() != together.size())
                return std::unexpected{ std::format("the view '{}' alone has {} bytes, together {}", view, pixels->size(), together.size()) };
            std::size_t differing{ 0 };
            for (std::size_t i = 0; i < pixels->size(); i += 4)
                differing += std::memcmp(pixels->data() + i, together.data() + i, 4) != 0 ? 1 : 0;
            if (differing > 0)
                return std::unexpected{ std::format("the view '{}' rendered alone differs from it rendered with the other in {} pixels", view, differing) };
            std::println("Views verified: '{}' rendered alone matches it rendered with the other", view);
            return {};
        };

        if (m_frame == togetherFrame)
        {
            for (const auto view : { mainView, sideView })
            {
                auto pixels = read(view);
                if (!pixels)
                    return std::unexpected{ pixels.error() };
                m_together.insert_or_assign(std::string{ view }, std::move(*pixels));
            }
            constexpr std::size_t minimum{ 100 };
            const auto inMain = unlitPixels(m_together.find(mainView)->second);
            const auto inSide = unlitPixels(m_together.find(sideView)->second);
            if (inMain != 0 || inSide < minimum)
                return std::unexpected{ std::format("the side cube shows in {} pixels of the main view and {} of the side view; it belongs in at least {} of the side view only", inMain, inSide, minimum) };
            std::println("Views verified: the side cube is drawn in {} pixels of the side view and culled from the main view", inSide);
            m_viewsShown = ViewsShown::Main;
        }
        else if (m_frame == togetherFrame + 2)
            return alone(mainView, ViewsShown::Side);
        else if (m_frame == togetherFrame + 4)
            return alone(sideView, ViewsShown::Both);
        return {};
    }

    // --blend, in the second frame (the camera has its global transform): two cubes of half-transparent, blended PBR
    // materials between the camera and the helmet, an orange one and a blue one, added in that order. The blue one sits
    // behind the orange one (--blend-swap: in front of it), covering part of it, so the last frame shows the far cube
    // through the near one and the helmet through both, composed correctly only if the far cube draws first.
    auto addBlendedCubes(FrameContext& frame) -> std::expected<void, std::string>
    {
        auto& registry = frame.world.registry();
        std::optional<GlobalTransform> camera;
        for (auto&& [entity, cameraComponent, transform] : registry.view<Camera, GlobalTransform>().each())
            camera = transform;
        if (!camera)
            return std::unexpected{ "the scene has no camera" };

        const glm::vec3 nearer = camera->location + camera->forward() * 1.1f + camera->right() * 0.12f;
        const glm::vec3 farther = camera->location + camera->forward() * 1.5f + camera->right() * 0.38f + camera->up() * 0.16f;
        const std::array<std::pair<glm::vec3, glm::vec3>, 2> cubes{ {
            { glm::vec3{ 1.0f, 0.25f, 0.1f }, m_options.blendSwap ? farther : nearer },
            { glm::vec3{ 0.1f, 0.35f, 1.0f }, m_options.blendSwap ? nearer : farther },
        } };
        for (const auto& [albedo, location] : cubes)
        {
            const auto created = frame.assets.createMaterial();
            if (!created)
                return std::unexpected{ describe(created.error()) };
            const auto& material = *created;
            const std::array<std::pair<std::string_view, assets::MaterialValue>, 3> parameters{ {
                { "albedo", albedo },
                { "roughness", 0.6f },
                { "alpha", 0.5f },
            } };
            for (const auto& [name, value] : parameters)
            {
                if (auto set = material->setParameter(std::string{ name }, value); !set)
                    return std::unexpected{ assets::describe(set.error()) };
            }
            material->setAlphaMode(assets::AlphaMode::Blend);

            auto cube = frame.assets.instantiateScene(frame.world, frame.types, assets::engine::cube, {});
            if (!cube)
                return std::unexpected{ describe(cube.error()) };
            auto& transform = registry.get<Transform>(*cube);
            transform.location = location;
            // Turned so that three faces, each lit differently, face the camera.
            transform.rotation = math::quatFromEuler(glm::vec3{ glm::radians(30.0f), 0.0f, glm::radians(40.0f) });
            transform.scale = glm::vec3{ 0.25f };
            for (const auto entity : subtree(registry, *cube))
            {
                if (registry.has<MeshRenderer>(entity))
                    registry.get<MeshRenderer>(entity).material = material;
                registry.markStaticDirty(entity);
            }
            if (!m_blended)
                m_blended = material.shared();
        }
        return {};
    }

    // --blend, in the last frame: the forward pass holds the pipeline of the blended cube's variant, and the G-buffer pass
    // (whose batches the shadow views' depth passes draw too) does not.
    auto verifyBlended(const FrameContext& frame) const -> std::expected<void, std::string>
    {
        if (!m_blended)
            return std::unexpected{ "no blended cube was added" };
        const auto variant = m_blended->variant();
        if (variant.alphaMode != assets::AlphaMode::Blend)
            return std::unexpected{ "the cube's material does not blend" };
        if (!frame.instances(frame.world).batchId(variant))
            return std::unexpected{ "no draw batch holds the blended cube" };
        if (!hasForwardPipeline(frame.frameGraph, variant))
            return std::unexpected{ "the forward pass has no pipeline for the blended cube" };
        if (hasGeometryPipeline(frame.frameGraph, variant))
            return std::unexpected{ "the G-buffer pass has a pipeline for the blended cube" };
        std::println("Blended material verified: drawn by the forward pass, not into the G-buffer");
        return {};
    }

    // --unlit, in the second frame (the camera has its global transform): an unlit cube to the right of a PBR one, side
    // by side in front of the main camera, both instances of the engine's cube. The main camera's PostProcess turns
    // bloom off and tone maps with Reinhard at exposure 1, so the unlit cube's colour reaches the last frame as verifyUnlit expects.
    auto addUnlitCubes(FrameContext& frame) -> std::expected<void, std::string>
    {
        auto& registry = frame.world.registry();
        std::optional<GlobalTransform> camera;
        for (auto&& [entity, cameraComponent, transform] : registry.view<Camera, GlobalTransform>().each())
            camera = transform;
        if (!camera)
            return std::unexpected{ "the scene has no camera" };

        auto unlitCreated = frame.assets.createMaterial(frame.assets.defaults().unlitTemplate);
        if (!unlitCreated)
            return std::unexpected{ describe(unlitCreated.error()) };
        auto& unlit = *unlitCreated;
        if (auto set = unlit->setParameter("color", unlitColor); !set)
            return std::unexpected{ assets::describe(set.error()) };
        const auto pbrCreated = frame.assets.createMaterial();
        if (!pbrCreated)
            return std::unexpected{ describe(pbrCreated.error()) };
        const auto& pbr = *pbrCreated;

        for (const auto& [material, side] : { std::pair{ unlit, 1.0f }, std::pair{ pbr, -1.0f } })
        {
            auto cube = frame.assets.instantiateScene(frame.world, frame.types, assets::engine::cube, {});
            if (!cube)
                return std::unexpected{ describe(cube.error()) };
            auto& transform = registry.get<Transform>(*cube);
            transform.location = camera->location + camera->forward() * 1.5f + camera->right() * (0.6f * side);
            transform.rotation = camera->rotation;
            transform.scale = glm::vec3{ 0.2f };
            for (const auto entity : subtree(registry, *cube))
            {
                if (registry.has<MeshRenderer>(entity))
                    registry.get<MeshRenderer>(entity).material = material;
                registry.markStaticDirty(entity);
            }
        }

        return showUnlitColor(frame.world);
    }

    // --double-sided, in the second frame (the camera has its global transform): a plane of an unlit, double-sided
    // material in front of the main camera, with its back to it. Its meshlets all face away from the camera, so the
    // GPU-driven path's normal cone test would drop them; drawn, the plane's colour reaches the last frame as
    // verifyBackFaces expects.
    auto addBackFacingPlane(FrameContext& frame) -> std::expected<void, std::string>
    {
        auto& registry = frame.world.registry();
        const auto mainCamera = frame.world.mainCamera();
        if (!mainCamera)
            return std::unexpected{ "the scene has no main camera" };
        const auto camera = registry.get<GlobalTransform>(mainCamera);

        auto created = frame.assets.createMaterial(frame.assets.defaults().unlitTemplate);
        if (!created)
            return std::unexpected{ describe(created.error()) };
        auto& material = *created;
        if (auto set = material->setParameter("color", unlitColor); !set)
            return std::unexpected{ assets::describe(set.error()) };
        material->setCullMode(rhi::CullMode::None);

        // plane.obj is 10 units square in its xy plane, its front facing +z.
        auto plane = instantiateModel(frame.world, "models/basic/plane.obj");
        if (!plane)
            return std::unexpected{ plane.error() };
        auto& transform = registry.get<Transform>(*plane);
        transform.location = camera.location + camera.forward() * 1.5f;
        // Turns the plane's front (+z) to the camera's forward (+y), away from the camera.
        transform.rotation = camera.rotation * glm::angleAxis(glm::radians(-90.0f), glm::vec3{ 1.0f, 0.0f, 0.0f });
        transform.scale = glm::vec3{ 0.05f };
        for (const auto entity : subtree(registry, *plane))
        {
            if (registry.has<MeshRenderer>(entity))
                registry.get<MeshRenderer>(entity).material = material;
            registry.markStaticDirty(entity);
        }
        return showUnlitColor(frame.world);
    }

    // --unlit, --views, --double-sided: the main camera's PostProcess turns bloom off and tone maps with Reinhard at
    // exposure 1, so an unlit colour reaches the last frame as unlitPixels expects.
    static auto showUnlitColor(world::World& world) -> std::expected<void, std::string>
    {
        const auto mainCamera = world.mainCamera();
        if (!mainCamera)
            return std::unexpected{ "the scene has no main camera" };
        auto& registry = world.registry();
        const PostProcess settings{ .toneMapping = ToneMapping::Reinhard, .exposure = 1.0f, .gamma = 2.2f, .bloomIntensity = 0.0f };
        if (registry.has<PostProcess>(mainCamera))
            registry.get<PostProcess>(mainCamera) = settings;
        else
            registry.add<PostProcess>(mainCamera, settings);
        return {};
    }

    // An instance of the model file 'path' (in the sample project) in the scene, as a root entity.
    auto instantiateModel(world::World& world, std::string_view path) -> std::expected<world::Entity, std::string>
    {
        const auto id = m_engine->assets().find(path);
        if (!id)
            return std::unexpected{ std::format("'{}' is not in the sample project", path) };
        auto holder = m_engine->assets().instantiateScene(world, m_engine->types(), *id, {});
        if (!holder)
            return std::unexpected{ describe(holder.error()) };
        return *holder;
    }

    // The UI pass's input: one window, so a screenshot shows ImGui drawn over the scene.
    void buildUi(const render_graph::FrameGraph& frameGraph) const
    {
        if (m_options.noOverlay)
            return;
        ImGui::SetNextWindowPos(ImVec2{ 10.0f, 10.0f });
        ImGui::Begin("aegis-test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Frame graph: %d passes", static_cast<int>(frameGraph.passes().size()));
        for (const auto& timing : frameGraph.timings())
            ImGui::Text("%-20s %.3f ms", timing.name.c_str(), timing.gpuMs);
        ImGui::End();
    }

    // --view: the view in a window of the UI, as an editor's viewport shows it, across the halfway resize.
    void showView(FrameContext& frame)
    {
        if (frame.views.empty())
            return;
        const auto& view = frame.views.front();
        const auto texture = frame.presentation.uiTexture(frame.frameGraph.pool().texture(view.final).view);
        if (texture == 0)
            return;
        const auto extent = view.extent;
        ImGui::SetNextWindowPos(ImVec2{ static_cast<float>(sceneExtent.x) / 2.0f, 10.0f });
        ImGui::Begin("View", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Image(ImTextureRef{ texture }, ImVec2{ static_cast<float>(extent.x) / 2.0f, static_cast<float>(extent.y) / 2.0f });
        ImGui::End();
        ++m_result.viewShown;
    }

    // The frame graph's own per-pass timestamps: one entry per pass, read back once complete.
    void verifyTimings(const render_graph::FrameGraph& frameGraph)
    {
        const auto timings = frameGraph.timings();
        if (timings.size() != frameGraph.passes().size())
            m_result.errors.push_back(std::format("Pass timings failed: {} timings for {} passes", timings.size(), frameGraph.passes().size()));
        for (const auto& [name, gpuMs, cpuMs] : timings)
            std::println("  {:<20} GPU {:7.4f} ms  CPU {:7.4f} ms", name, gpuMs, cpuMs);
    }

    // --mutate-scene: changes the scene the way an editor or gameplay code would, while frames are in
    // flight, and checks that the GPU-driven path follows. Called once per frame at the safe point.
    // Frame 30 loads a second model, 60 destroys the first one, 90 edits the second one's materials and
    // 120 sets them to cull nothing, adding a dynamic copy of one of its parts in the same frame. The frame
    // after each of the first two, the instance pass must have written as many static instances as the scene
    // holds; after the last, every instance of those materials, static and dynamic, must be in their new
    // variant's batch, with a pipeline for it. The last frame (--screenshot) shows the second model, in red,
    // and the copy at the model's origin.
    auto mutateScene(FrameContext& frame) -> std::expected<void, std::string>
    {
        constexpr std::uint32_t loadFrame{ 30 };
        constexpr std::uint32_t destroyFrame{ 60 };
        constexpr std::uint32_t editFrame{ 90 };
        constexpr std::uint32_t cullFrame{ 120 };
        auto& registry = frame.world.registry();

        // The frame after a change: the instances written for it match the scene.
        const auto verifyWritten = [&](std::string_view what, bool grew) -> std::expected<void, std::string> {
            const auto expected = drawableStatic(registry);
            if (grew ? expected <= m_staticDrawn : expected >= m_staticDrawn)
                return std::unexpected{ std::format("{} did not change the scene's static instances ({} before, {} after)", what, m_staticDrawn, expected) };
            if (const auto written = writtenStatic(frame.frameGraph); written && *written != expected)
                return std::unexpected{ std::format("after {}: {} static instances written, the scene has {}", what, *written, expected) };
            std::println("Scene change verified: {} -> {} static instances after {}", m_staticDrawn, expected, what);
            m_staticDrawn = expected;
            return {};
        };

        if (m_frame == loadFrame)
        {
            m_staticDrawn = drawableStatic(registry);
            auto model = instantiateModel(frame.world, "models/sci_fi_helmet/SciFiHelmet.gltf");
            if (!model)
                return std::unexpected{ std::format("Loading the second model: {}", model.error()) };
            m_addedModel = *model;
            registry.get<Transform>(m_addedModel).location = glm::vec3{ 0.9f, 0.0f, 0.0f };
            registry.markStaticDirty(m_addedModel);
        }
        else if (m_frame == loadFrame + 1)
        {
            return verifyWritten("loading a model", true);
        }
        else if (m_frame == destroyFrame)
        {
            destroySubtree(registry, m_helmet);
        }
        else if (m_frame == destroyFrame + 1)
        {
            return verifyWritten("destroying a model", false);
        }
        else if (m_frame == editFrame)
        {
            std::uint32_t edited{ 0 };
            for (auto&& [entity, renderer] : registry.view<MeshRenderer>().each())
            {
                if (renderer.material && renderer.material->setParameter("albedo", glm::vec3{ 1.0f, 0.1f, 0.1f }))
                    ++edited;
            }
            if (edited == 0)
                return std::unexpected{ "no material to edit" };
        }
        else if (m_frame == cullFrame)
        {
            m_culled.clear();
            std::optional<MeshRenderer> part;
            for (auto&& [entity, renderer] : registry.view<MeshRenderer>().each())
            {
                if (!renderer.mesh || !renderer.material || std::ranges::contains(m_culled, renderer.material.shared()))
                    continue;
                renderer.material->setCullMode(rhi::CullMode::None);
                m_culled.push_back(renderer.material.shared());
                part = renderer;
            }
            if (!part)
                return std::unexpected{ "no material to set to cull nothing" };
            // A new instance of a material whose variant changed is added in the new batch at once; the static
            // instances of that material must follow it there.
            const auto copy = registry.create("Culled Copy", glm::vec3{ 0.9f, 0.0f, 0.0f });
            registry.add<MeshRenderer>(copy, *part);
            registry.add<DynamicTag>(copy);
        }
        else if (m_frame == cullFrame + 1)
        {
            return verifyCulled(frame);
        }
        return {};
    }

    // The frame after the materials were set to cull nothing: every instance of each is in the batch of its new
    // variant, and the geometry pass created that variant's pipeline.
    auto verifyCulled(const FrameContext& frame) const -> std::expected<void, std::string>
    {
        std::array<std::uint32_t, 2> moved{}; // Static, dynamic
        const auto& instances = frame.instances(frame.world);
        for (const auto& material : m_culled)
        {
            const auto variant = material->variant();
            const auto batch = instances.batchId(variant);
            if (!batch)
                return std::unexpected{ "a material set to cull nothing has no batch for its variant" };
            for (const bool dynamic : { false, true })
            {
                for (const auto& instance : dynamic ? instances.dynamicInstances() : instances.staticInstances())
                {
                    if (instance.material != material)
                        continue;
                    if (instance.drawBatch != *batch)
                        return std::unexpected{ std::format("a {} instance of a material set to cull nothing is in batch {}, its variant's is {}", dynamic ? "dynamic" : "static", instance.drawBatch, *batch) };
                    ++moved[dynamic ? 1 : 0];
                }
            }
            if (!hasGeometryPipeline(frame.frameGraph, variant))
                return std::unexpected{ "the geometry pass has no pipeline for a material set to cull nothing" };
        }
        if (moved[0] == 0 || moved[1] == 0)
            return std::unexpected{ std::format("materials set to cull nothing have {} static and {} dynamic instances, expected some of each", moved[0], moved[1]) };
        std::println("Variant change verified: {} material(s) set to cull nothing, {} static and {} dynamic instance(s) in their new batches, drawn with their pipelines",
            m_culled.size(), moved[0], moved[1]);
        return {};
    }

    // --worlds: a second world from the scene file, with the helmet left out so it draws fewer instances. It updates
    // once, in the first frame (it begins: its transforms and camera matrices are set), and then renders in the middle
    // third of the frames without updating. The main world updates in every frame and renders in the others, the
    // last one among them (--compare checks it against the plain scene's reference). The frame after each switch, the
    // instance pass must have written the static instances of the world it rendered. Called once per frame at the
    // safe point; without a frame count (a windowed run until closed) the second world never renders.
    auto switchWorlds(FrameContext& frame) -> std::expected<void, std::string>
    {
        const auto shownFrom = m_options.frames / 3;
        const auto shownUntil = 2 * m_options.frames / 3;
        if (m_frame == 0)
        {
            const auto scene = m_engine->assets().find(m_options.sceneFile);
            if (!scene || !m_helmet)
                return std::unexpected{ std::format("{} is not in the sample project, or has no helmet", m_options.sceneFile) };
            auto& second = frame.engine.createWorld();
            if (auto created = m_engine->assets().instantiateScene(second, m_engine->types(), *scene); !created)
            {
                frame.engine.destroyWorld(second);
                return std::unexpected{ std::format("Loading the second world: {}", describe(created.error())) };
            }
            // The scene's entities keep their IDs in every world it is instantiated into.
            auto& registry = second.registry();
            destroySubtree(registry, registry.find(frame.world.registry().get<StableId>(m_helmet)));
            frame.update(second, world::UpdateMode::Play);
            m_second = &second;
        }
        if (!m_second || m_options.frames == 0)
            return {};

        if (m_frame >= shownFrom && m_frame < shownUntil)
            frame.render(*m_second);
        if (m_frame == shownFrom + 1 || m_frame == shownUntil + 1)
        {
            const bool second = m_frame == shownFrom + 1;
            const auto expected = drawableStatic(second ? m_second->registry() : frame.world.registry());
            const auto* name = second ? "second" : "main";
            if (const auto written = writtenStatic(frame.frameGraph); written && *written != expected)
                return std::unexpected{ std::format("{} static instances written, the {} world has {}", *written, name, expected) };
            std::println("World switch verified: the {} world rendered, {} static instances", name, expected);
        }
        if (m_frame + 1 == m_options.frames)
        {
            const auto secondUpdates = m_second->clock().frame();
            const auto mainUpdates = frame.world.clock().frame();
            if (secondUpdates != 1 || mainUpdates != m_frame)
                return std::unexpected{ std::format("in {} frames the second world updated {} times, the main world {}", m_frame, secondUpdates, mainUpdates) };
            std::println("Worlds verified: in {} frames the second world updated once, the main world in each", m_frame);
        }
        return {};
    }

    const Options& m_options;
    SceneResult& m_result;
    Engine* m_engine{ nullptr };
    world::World* m_world{ nullptr };
    std::uint32_t m_frame{ 0 };                                            // Frames begun so far
    world::Entity m_helmet;                                                // The holder of the model instance the scene file holds
    world::Entity m_addedModel;                                            // --mutate-scene: the holder of the model instance loaded later
    world::Entity m_viewCamera;                                            // --view, --views: the camera the view renders through, not the main one
    ViewsShown m_viewsShown{ ViewsShown::Both };                           // --views: the views rendered
    std::map<std::string, std::vector<std::byte>, std::less<>> m_together; // --views: each view's image, rendered with the other
    std::uint32_t m_staticDrawn{ 0 };                                      // --mutate-scene: drawable static entities before the last change
    std::vector<std::shared_ptr<assets::Material>> m_culled;               // --mutate-scene: the materials set to cull nothing
    std::shared_ptr<assets::Material> m_blended;                           // --blend: the first blended cube's material
    world::World* m_second{ nullptr };                                     // --worlds: the second world, from the same scene file
};

// --unlit: the unlit cube shows its colour, (0, 1, 1), as it is: Reinhard maps 1 to 0.5, which gamma 2.2 or an sRGB
// target encodes as about 186 to 188, and 0 stays exactly 0 in red, which any light on the cube would raise. Its
// pixels must cover a good part of the frame.
auto verifyUnlit(std::span<const std::byte> pixels) -> std::expected<void, std::string>
{
    std::size_t count{ 0 };
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        const auto red = std::to_integer<int>(pixels[i]);
        const auto green = std::to_integer<int>(pixels[i + 1]);
        const auto blue = std::to_integer<int>(pixels[i + 2]);
        if (red == 0 && green >= 184 && green <= 190 && blue >= 184 && blue <= 190)
            ++count;
    }
    constexpr std::size_t minimum{ 1000 };
    if (count < minimum)
        return std::unexpected{ std::format("{} pixels show the unlit cube's colour unlit, fewer than {}", count, minimum) };
    std::println("Unlit material verified: {} pixels show the unlit cube's colour unlit", count);
    return {};
}

// --double-sided: the plane, seen from behind, shows its colour as --unlit's cube does (see verifyUnlit) over a good
// part of the frame: its back faces were drawn, on the GPU-driven path too.
auto verifyBackFaces(std::span<const std::byte> pixels) -> std::expected<void, std::string>
{
    const auto count = unlitPixels(pixels);
    constexpr std::size_t minimum{ 10000 };
    if (count < minimum)
        return std::unexpected{ std::format("{} pixels show the double-sided plane's back, fewer than {}", count, minimum) };
    std::println("Double-sided material verified: {} pixels show the plane's back", count);
    return {};
}

// Headless: the last frame, as the engine saved it, is drawn (not one flat color), has the size it should have and,
// with --compare, matches the reference.
auto verifyFrame(const Options& options, const std::filesystem::path& file, rhi::Extent2D extent) -> std::expected<void, std::string>
{
    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image{
        stbi_load(file.string().c_str(), &width, &height, &channels, 4), &stbi_image_free
    };
    if (!image)
        return std::unexpected{ std::format("failed to read the frame '{}'", file.string()) };
    if (std::cmp_not_equal(width, extent.x) || std::cmp_not_equal(height, extent.y))
        return std::unexpected{ std::format("the frame is {}x{}, the {} {}x{}", width, height, options.view || options.views ? "view" : "target", extent.x, extent.y) };
    const auto pixels = std::as_bytes(std::span{ image.get(), std::size_t{ extent.x } * extent.y * 4 });

    if (!options.compare.empty())
    {
        if (auto matched = compareToReference(options, pixels, extent); !matched)
            return matched;
    }

    if (options.unlit)
    {
        if (auto unlit = verifyUnlit(pixels); !unlit)
            return unlit;
    }

    if (options.doubleSided)
    {
        if (auto backFaces = verifyBackFaces(pixels); !backFaces)
            return backFaces;
    }

    const auto texel = [&](std::uint32_t x, std::uint32_t y) {
        const auto offset = (std::size_t{ y } * extent.x + x) * 4;
        return std::array{ pixels[offset], pixels[offset + 1], pixels[offset + 2] };
    };
    if (texel(extent.x / 2, extent.y / 2) == texel(0, 0))
        return std::unexpected{ "the scene frame's centre and corner have the same color" };
    std::println("Offscreen scene frame verified: {}x{} {} read back, not uniform", extent.x, extent.y, options.view || options.views ? "view" : "target");
    return {};
}
}

auto runScene(const Options& options) -> int
{
    // Headless, the engine saves the last frame for the checks: to --screenshot, or to a file of its own.
    std::filesystem::path frameFile;
    if (options.headless)
    {
        frameFile = options.screenshot;
        if (frameFile.empty())
            frameFile = std::filesystem::temp_directory_path() / std::format("aegis-test-{:016x}.png", std::random_device{}() * 0x100000001ull);
    }

    auto engine = Engine::create({
        .title = "aegis-test",
        .width = sceneExtent.x,
        .height = sceneExtent.y,
        .headless = options.headless,
        .frames = options.frames,
        .screenshot = frameFile.string(),
        .screenshotView = true,
        .gpuDriven = options.gpuDriven,
    });
    if (!engine)
    {
        std::println("Failed to create the engine\n{}", describe(engine.error()));
        return exitFailure;
    }
    if (options.gpuDriven && !(*engine)->useGpuDrivenRendering())
    {
        std::println("{}", skipMeshShaders);
        return skipExitCode;
    }

    SceneResult result;
    (*engine)->setShell(std::make_unique<SceneShell>(options, result));
    const auto scene = (*engine)->assets().find(options.sceneFile);
    if (!scene)
    {
        std::println("'{}' is not in the sample project", options.sceneFile);
        return exitFailure;
    }
    if (auto loaded = (*engine)->loadScene(*scene); !loaded)
    {
        std::println("{}", describe(loaded.error()));
        return exitFailure;
    }

    auto exitCode = (*engine)->run();
    engine->reset(); // The shell has finished with the result
    for (const auto& error : result.errors)
    {
        std::println("{}", error);
        exitCode = exitFailure;
    }
    // The graph follows the view from the first frame on, so the UI can show it in every frame.
    if (options.view && options.frames != 0)
    {
        if (result.viewShown != options.frames)
        {
            std::println("UI texture check failed: the UI showed the view in {} of {} frames", result.viewShown, options.frames);
            exitCode = exitFailure;
        }
        else
            std::println("UI texture verified: the view shown in {} frames, across a resize", result.viewShown);
    }

    if (options.headless && exitCode == exitSuccess)
    {
        if (options.views)
        {
            // One screenshot per view, named after it.
            for (const auto& [view, extent] : { std::pair{ mainView, viewExtent }, std::pair{ sideView, sideViewExtent } })
            {
                const auto file = frameFile.parent_path() / std::format("{}-{}{}", frameFile.stem().string(), view, frameFile.extension().string());
                if (auto verified = verifyFrame(options, file, extent); !verified)
                {
                    std::println("Offscreen frame check failed for the view '{}': {}", view, verified.error());
                    exitCode = exitFailure;
                }
                if (options.screenshot.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(file, ignored);
                }
            }
        }
        else if (auto verified = verifyFrame(options, frameFile, options.view ? result.view : sceneExtent); !verified)
        {
            std::println("Offscreen frame check failed: {}", verified.error());
            exitCode = exitFailure;
        }
    }
    if (options.headless && options.screenshot.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(frameFile, ignored);
    }
    return exitCode;
}
}

module;
#include <expected>
#include <string>
#include <utility>

module aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
StaticMeshRenderSystem::StaticMeshRenderSystem(assets::MaterialType type, std::string shader) :
    m_type{ type },
    m_shader{ std::move(shader) }
{
}

auto StaticMeshRenderSystem::createResources(rhi::Device& device, const RenderTargets& targets)
    -> std::expected<void, Error>
{
    m_device = &device;
    auto pipeline = createGraphicsPipeline(device, m_shader,
        rhi::Pipeline::GraphicsDesc{
            .name = "StaticMeshes",
            .colorAttachments = targets.colorAttachments,
            .depthAttachment = targets.depthAttachment,
        });
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto StaticMeshRenderSystem::render(const RenderContext& context) -> void
{
    const auto& cmd = context.cmd;
    cmd.bindPipeline(*m_pipeline);

    const assets::MaterialInstance* lastMaterial{ nullptr };
    auto view = context.scene.registry().view<Aegis::GlobalTransform, assets::Mesh, assets::Material>();
    for (auto&& [entity, transform, mesh, material] : view.each())
    {
        if (!mesh.staticMesh || !material.instance || material.instance->materialTemplate()->type() != m_type)
            continue;

        if (lastMaterial != material.instance.get())
        {
            material.instance->update(context.frameIndex);
            lastMaterial = material.instance.get();
        }

        // Row-major for the shader; the normal matrix's transpose is implied by the row layout.
        const auto model = transform.matrix();
        const auto normalMatrix = glm::inverse(glm::mat3{ model });
        cmd.pushConstants(PushConstants{
            .model = glm::mat3x4{ glm::rowMajor4(model) },
            .normalRow0 = normalMatrix[0],
            .normalRow1 = normalMatrix[1],
            .normalRow2 = normalMatrix[2],
            .camera = context.camera,
            .mesh = mesh.staticMesh->meshDataAddress(),
            .material = material.instance->parameterAddress(context.frameIndex),
        });

        cmd.bindIndexBuffer(m_device->get(mesh.staticMesh->indexBuffer()));
        cmd.drawIndexed(mesh.staticMesh->indexCount());
    }
}
}

module;
#include <cassert>

module aegis.render_passes;
import std;
import aegis.rhi;

namespace aegis::render_graph
{
UiPass::UiPass(ResourceDeclarations& declarations, Desc desc) :
    m_backend{ desc.backend },
    m_targetFormat{ desc.targetFormat }
{
    assert(!desc.finals.empty() && "The UI pass draws over or shows at least one image");
    // Onto the frame's target the reads only order the scene passes first and ready each "Final" to be shown.
    for (const auto& final : desc.finals)
    {
        m_finals.push_back(declarations.addReference(final, desc.targetFormat ? Usage::SampledFragment : Usage::ColorAttachment));
        if (!desc.targetFormat)
            break; // Drawn over: only the first
    }
}

auto UiPass::name() const -> std::string_view
{
    return "UI";
}

auto UiPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    if (!m_backend)
        return makeError(ErrorCode::InitializationFailed, "The UI pass needs the rhi::ImGuiBackend");
    const auto targetFormat = m_targetFormat.value_or(pool.format(m_finals.front()));
    if (auto set = m_backend->setTargetFormat(targetFormat); !set)
        return makeError(ErrorCode::InitializationFailed, std::format("The UI pass cannot draw onto a {} target", rhi::formatName(targetFormat)), set.error());
    return {};
}

auto UiPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // The caller ended the frame (ImGui::Render); without one there is nothing to draw. An empty frame
    // still goes through the backend, which uploads the font atlas and other textures with it.
    // The frame's target is cleared even then: nothing else draws onto it.
    const bool draws = m_backend->hasDrawData();
    if (!draws && !m_targetFormat)
        return;

    const auto& cmd = frameInfo.cmd;
    std::array colorAttachments{ m_targetFormat
                                     ? rhi::Attachment::color(frameInfo.target, rhi::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f })
                                     : rhi::Attachment::colorLoad(pool.texture(m_finals.front()).view) };
    cmd.beginRendering({ .colorAttachments = colorAttachments });
    if (draws)
        m_backend->record(cmd);
    cmd.endRendering();
}
}

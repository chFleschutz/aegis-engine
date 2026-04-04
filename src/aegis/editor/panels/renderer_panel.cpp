#include "pch.h"
#include "renderer_panel.h"

#include "engine.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/frame_graph/frame_graph_resource_pool.h"
#include "graphics/renderer.h"

#include <imgui.h>

namespace Aegis::UI
{
	static auto toTypeString(Graphics::FGResource::Info type) -> const char*
	{
		if (std::holds_alternative<Graphics::FGBufferInfo>(type))
			return "Buffer";
		if (std::holds_alternative<Graphics::FGTextureInfo>(type))
			return "Texture";
		if (std::holds_alternative<Graphics::FGReferenceInfo>(type))
			return "Reference";
		return "Unknown";
	}

	static auto toString(VkFormat format) -> const char*
	{
		switch (format)
		{
		default:
			return "Undefined";
		case VK_FORMAT_UNDEFINED:
			return "Undefined";
		case VK_FORMAT_R8G8B8A8_UNORM:
			return "RGBA8_UNORM";
		case VK_FORMAT_B8G8R8A8_UNORM:
			return "BGRA8_UNORM";
		case VK_FORMAT_R8G8B8A8_SRGB:
			return "RGBA8_SRGB";
		case VK_FORMAT_B8G8R8A8_SRGB:
			return "BGRA8_SRGB";
		case VK_FORMAT_R16G16B16A16_SFLOAT:
			return "RGBA16_SFLOAT";
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			return "RGBA32_SFLOAT";
		case VK_FORMAT_D32_SFLOAT:
			return "D32_SFLOAT";
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return "D32_SFLOAT_S8_UINT";
		}
	}

	void RendererPanel::draw()
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10, 30), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
		if (!ImGui::Begin("Renderer"))
		{
			ImGui::End();
			return;
		}

		auto& renderer = Engine::instance().renderer();
		auto& frameGraph = renderer.frameGraph();
		auto& resourcePool = frameGraph.resourcePool();

		// Renderer info
		ImGui::SeparatorText("Renderer Info");

		ImGui::Text("Extent: %d x %d", renderer.swapChain().extent().width, renderer.swapChain().extent().height);
		ImGui::Text("Aspect Ratio: %.2f", renderer.aspectRatio());

		ImGui::NewLine();
		ImGui::SeparatorText("Render Passes");

		// Frame graph render passes
		int i = 0;
		for (const auto& nodeHandle : frameGraph.nodes())
		{
			ImGui::PushID(i);

			auto& node = resourcePool.node(nodeHandle);
			if (ImGui::CollapsingHeader(node.info.name.c_str(), ImGuiTreeNodeFlags_None))
			{
				node.pass->drawUI();

				if (!node.info.reads.empty() && ImGui::TreeNode("Reads"))
				{
					for (const auto& inputHandle : node.info.reads)
					{
						auto& input = resourcePool.resource(inputHandle);
						ImGui::BulletText("%s", input.name.c_str());
					}
					ImGui::TreePop();
				}

				if (!node.info.writes.empty() && ImGui::TreeNode("Writes"))
				{
					for (const auto& outputHandle : node.info.writes)
					{
						auto& input = resourcePool.resource(outputHandle);
						ImGui::BulletText("%s", input.name.c_str());
					}
					ImGui::TreePop();
				}
			}

			ImGui::PopID();
			i++;
		}

		ImGui::NewLine();
		ImGui::SeparatorText("Frame Graph Resources");

		// Frame graph resources
		const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH;
		const ImVec2 outer_size = ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10);
		if (ImGui::BeginTable("Resources", 4, flags, outer_size))
		{
			ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
			ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			const auto& resources = resourcePool.resources();
			for (size_t i = 0; i < resources.size(); i++)
			{
				const auto& res = resources[i];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%d", i);
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", res.name.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", toTypeString(res.info));
				ImGui::TableSetColumnIndex(3);
				if (auto info = std::get_if<Graphics::FGTextureInfo>(&res.info))
				{
					ImGui::Text("%s", toString(info->format));
				}
			}

			ImGui::EndTable();
		}

		ImGui::End();
	}
}

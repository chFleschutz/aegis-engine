#pragma once

#include "core/profiler.h"

#include <imgui.h>

namespace Aegis::UI
{
	class ProfilerPanel
	{
	public:
		void draw()
		{
			ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 10), ImGuiCond_FirstUseEver, ImVec2(0, 1));
			if (!ImGui::Begin("Profiler"))
			{
				ImGui::End();
				return;
			}

			auto& profiler = Profiler::instance();
			double frameTime = profiler.time("Frame Time");

			ImGui::Text("Frame Time: %.3f ms", frameTime);
			ImGui::Text("FPS: %.1f", 1000.0 / frameTime);

			ImGui::Spacing();

			const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH;
			const ImVec2 outer_size = ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10);
			if (ImGui::BeginTable("CPU Times", 3, flags, outer_size))
			{
				ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
				ImGui::TableSetupColumn("CPU Time", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Frame Percent (%)", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				for (const auto& [name, rolling] : profiler.times())
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%s", name.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%7.3f", rolling.average());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%7.2f", rolling.average() / frameTime * 100.0);
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();

			if (ImGui::BeginTable("GPU Times", 3, flags, outer_size))
			{
				ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
				ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Frame Percent (%)", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				for (const auto& timing : Graphics::GPUTimerManager::instance().timings())
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%s", timing.name.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%7.3f", timing.timeMs);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%7.2f", timing.timeMs / frameTime * 100.0);
				}
				ImGui::EndTable();
			}

			ImGui::End();
		}
	};
}
module;

#include "core/assert.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

export module Aegis.Graphics.MaterialInstance;

import Aegis.Graphics.Descriptors;
import Aegis.Graphics.MaterialTemplate;
import Aegis.Graphics.Bindless;
import Aegis.Graphics.Buffer;
import Aegis.Graphics.Texture;
import Aegis.Graphics.Globals;
import Aegis.Core.Asset;

export namespace Aegis::Graphics
{
	class MaterialInstance : public Core::Asset
	{
	public:
		static auto create(std::shared_ptr<MaterialTemplate> materialTemplate) -> std::shared_ptr<MaterialInstance>
		{
			return std::make_shared<MaterialInstance>(std::move(materialTemplate));
		}

		MaterialInstance(std::shared_ptr<MaterialTemplate> materialTemplate) :
			m_template(std::move(materialTemplate)),
			m_uniformBuffer{ Buffer::uniformBuffer(m_template->parameterSize(), MAX_FRAMES_IN_FLIGHT) }
		{
			AGX_ASSERT_X(m_template, "Material template cannot be null");

			m_dirtyFlags.fill(true);
		}

		MaterialInstance(const MaterialInstance&) = delete;
		MaterialInstance(MaterialInstance&&) = delete;
		~MaterialInstance() = default;

		auto operator=(const MaterialInstance&) -> MaterialInstance & = delete;
		auto operator=(MaterialInstance&&) noexcept -> MaterialInstance & = delete;

		template<typename T>
		[[nodiscard]] auto queryParameter(const std::string& name) const -> T
		{
			return std::get<T>(queryParameter(name));
		}

		[[nodiscard]] auto materialTemplate() const -> std::shared_ptr<MaterialTemplate> { return m_template; }
		[[nodiscard]] auto queryParameter(const std::string& name) const -> MaterialParameter::Value
		{
			auto it = m_overrides.find(name);
			if (it != m_overrides.end())
				return it->second;

			return m_template->queryDefaultParameter(name);
		}

		[[nodiscard]] auto buffer() const -> const Bindless::BindlessFrameBuffer& { return m_uniformBuffer; }

		void setParameter(const std::string& name, const MaterialParameter::Value& value)
		{
			AGX_ASSERT_X(m_template->hasParameter(name), "Material parameter not found");
			m_overrides[name] = value;
			m_dirtyFlags.fill(true);
		}

		void updateParameters(int index)
		{
			if (!m_dirtyFlags[index])
				return;

			// Update uniform buffer
			for (const auto& [name, info] : m_template->parameters())
			{
				const MaterialParameter::Value* valuePtr = &info.defaultValue;
				auto it = m_overrides.find(name);
				if (it != m_overrides.end())
				{
					valuePtr = &it->second;
				}

				if (auto texture = std::get_if<std::shared_ptr<Texture>>(valuePtr))
				{
					auto handle = (*texture)->sampledDescriptorHandle();
					AGX_ASSERT_X(handle.isValid(), "Invalid texture descriptor handle in MaterialInstance!");
					m_uniformBuffer.write(&handle, info.size, info.offset, index);
				}
				else
				{
					m_uniformBuffer.write(valuePtr, info.size, info.offset, index);
				}
			}

			m_dirtyFlags[index] = false;
		}

	private:
		std::shared_ptr<MaterialTemplate> m_template;
		std::unordered_map<std::string, MaterialParameter::Value> m_overrides;
		Bindless::BindlessFrameBuffer m_uniformBuffer;
		std::array<bool, MAX_FRAMES_IN_FLIGHT> m_dirtyFlags;
	};
}

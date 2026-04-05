module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <array>

export module Aegis.Graphics.Bindless:BindlessBuffer;

import :DescriptorHandle;

import Aegis.Graphics.Globals;
import Aegis.Graphics.Buffer;

export namespace Aegis::Graphics::Bindless
{
	static auto allocateBindlessHandle(const Buffer& buffer) -> DescriptorHandle
	{
		auto& bindlessSet = Engine::renderer().bindlessDescriptorSet();
		if (buffer.usage() & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		{
			return bindlessSet.allocateStorageBuffer(buffer.descriptorBufferInfo());
		}
		else if (buffer.usage() & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
		{
			return bindlessSet.allocateUniformBuffer(buffer.descriptorBufferInfo());
		}
		else
		{
			AGX_ASSERT_X(false, "allocateBindlessHandle only supports storage and uniform buffers");
			return DescriptorHandle{};
		}
	}

	static auto allocateBindlessHandle(const Buffer& buffer, uint32_t index) -> DescriptorHandle
	{
		auto& bindlessSet = Engine::renderer().bindlessDescriptorSet();
		if (buffer.usage() & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
		{
			return bindlessSet.allocateStorageBuffer(buffer.descriptorBufferInfoFor(index));
		}
		else if (buffer.usage() & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
		{
			return bindlessSet.allocateUniformBuffer(buffer.descriptorBufferInfoFor(index));
		}
		else
		{
			AGX_ASSERT_X(false, "allocateBindlessHandles only supports storage and uniform buffers");
			return DescriptorHandle{};
		}
	}


	/// @brief Encapsulates a buffer with a single descriptor handle
	export class BindlessBuffer
	{
	public:
		BindlessBuffer() = default;
		explicit BindlessBuffer(const Buffer::CreateInfo& bufferInfo) :
			m_buffer{ bufferInfo },
			m_handle{ allocateBindlessHandle(m_buffer) }
		{}

		BindlessBuffer(const BindlessBuffer&) = delete;
		BindlessBuffer(BindlessBuffer&& other) noexcept :
			m_buffer{ std::move(other.m_buffer) },
			m_handle{ other.m_handle }
		{
			other.m_handle.invalidate();
		}

		~BindlessBuffer()
		{
			Engine::renderer().bindlessDescriptorSet().freeHandle(m_handle);
		}

		auto operator=(const BindlessBuffer&) -> BindlessBuffer & = delete;
		auto operator=(BindlessBuffer&& other) noexcept -> BindlessBuffer&
		{
			if (this != &other)
			{
				Engine::renderer().bindlessDescriptorSet().freeHandle(m_handle);
				m_buffer = std::move(other.m_buffer);
				m_handle = other.m_handle;
				other.m_handle.invalidate();
			}
			return *this;
		}

		operator Buffer& () { return m_buffer; }
		operator const Buffer& () const { return m_buffer; }

		[[nodiscard]] auto buffer() -> Buffer& { return m_buffer; }
		[[nodiscard]] auto buffer() const -> const Buffer& { return m_buffer; }
		[[nodiscard]] auto handle() const -> DescriptorHandle { return m_handle; }

	private:
		Buffer m_buffer;
		DescriptorHandle m_handle;
	};


	/// @brief Encapsulates a buffer with multiple descriptor handles (one per buffer instance)
	export class BindlessMultiBuffer
	{
	public:
		BindlessMultiBuffer() = default;
		explicit BindlessMultiBuffer(const Buffer::CreateInfo& bufferInfo) :
			m_buffer{ bufferInfo }
		{
			m_handles.resize(bufferInfo.instanceCount);
			for (uint32_t i = 0; i < bufferInfo.instanceCount; i++)
			{
				m_handles[i] = allocateBindlessHandle(m_buffer, i);
			}
		}

		BindlessMultiBuffer(const BindlessMultiBuffer&) = delete;
		BindlessMultiBuffer(BindlessMultiBuffer&& other) noexcept :
			m_buffer{ std::move(other.m_buffer) },
			m_handles{ std::move(other.m_handles) }
		{
			other.m_handles.clear();
		}

		~BindlessMultiBuffer()
		{
			auto& bindlessSet = Engine::renderer().bindlessDescriptorSet();
			for (auto& handle : m_handles)
			{
				bindlessSet.freeHandle(handle);
			}
		}

		auto operator=(const BindlessMultiBuffer&) -> BindlessMultiBuffer & = delete;
		auto operator=(BindlessMultiBuffer&& other) noexcept -> BindlessMultiBuffer&
		{
			if (this != &other)
			{
				auto& bindlessSet = Engine::renderer().bindlessDescriptorSet();
				for (auto& handle : m_handles)
				{
					bindlessSet.freeHandle(handle);
				}
				m_buffer = std::move(other.m_buffer);
				m_handles = std::move(other.m_handles);
				other.m_handles.clear();
			}
			return *this;
		}

		[[nodiscard]] auto buffer() -> Buffer& { return m_buffer; }
		[[nodiscard]] auto buffer() const -> const Buffer& { return m_buffer; }
		[[nodiscard]] auto handle(size_t index = 0) const -> DescriptorHandle
		{
			AGX_ASSERT_X(index < m_handles.size(), "BindlessMultiBuffer handle index out of bounds");
			return m_handles[index];
		}

	private:
		Buffer m_buffer;
		std::vector<DescriptorHandle> m_handles;
	};


	/// @brief Encapsulates a buffer with multiple descriptor handles (one per frame in flight)
	export class BindlessFrameBuffer
	{
	public:
		BindlessFrameBuffer() = default;
		explicit BindlessFrameBuffer(const Buffer::CreateInfo& bufferInfo) :
			m_buffer{ bufferInfo }
		{
			AGX_ASSERT_X(bufferInfo.instanceCount == MAX_FRAMES_IN_FLIGHT,
				"BindlessBufferArray requires instanceCount to be equal to MAX_FRAMES_IN_FLIGHT");

			auto& bindlessSet = Engine::renderer().bindlessDescriptorSet();
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			{
				m_handles[i] = allocateBindlessHandle(m_buffer, static_cast<uint32_t>(i));
			}
		}

		BindlessFrameBuffer(const BindlessFrameBuffer&) = delete;
		BindlessFrameBuffer(BindlessFrameBuffer&&) = default;
		~BindlessFrameBuffer()
		{
			auto& bindlessSet = Engine::renderer().bindlessDescriptorSet();
			for (auto& handle : m_handles)
			{
				bindlessSet.freeHandle(handle);
			}
		}

		[[nodiscard]] auto buffer() -> Buffer& { return m_buffer; }
		[[nodiscard]] auto buffer() const -> const Buffer& { return m_buffer; }
		[[nodiscard]] auto handle(size_t index) const -> DescriptorHandle
		{
			AGX_ASSERT_X(index < MAX_FRAMES_IN_FLIGHT, "Requested handle index exceeds MAX_FRAMES_IN_FLIGHT");
			return m_handles[index];
		}

		void write(const void* data, VkDeviceSize size, VkDeviceSize offset, size_t index)
		{
			AGX_ASSERT_X(index < MAX_FRAMES_IN_FLIGHT, "BindlessFrameBuffer write index out of bounds");
			m_buffer.write(data, size, index * m_buffer.alignmentSize() + offset);
		}

	private:
		Buffer m_buffer;
		std::array<DescriptorHandle, MAX_FRAMES_IN_FLIGHT> m_handles;
	};
}

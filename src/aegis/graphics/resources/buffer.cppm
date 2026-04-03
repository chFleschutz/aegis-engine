module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <cstdint>
#include <utility>
#include <vector>

export module Aegis.Graphics.Buffer;

import Aegis.Graphics.Globals;
import Aegis.Graphics.VulkanContext;

export namespace Aegis::Graphics
{
	/// @brief Encapsulates a vulkan buffer
	class Buffer
	{
	public:
		struct CreateInfo
		{
			VkDeviceSize instanceSize{ 0 };
			uint32_t instanceCount{ 1 };
			VkBufferUsageFlags usage{ 0 };
			VmaAllocationCreateFlags allocFlags{ 0 };
			VkDeviceSize minOffsetAlignment{ 0 };
		};

		/// @brief Factory methods for common buffer types
		/// @note Use multiple instances only if intended to be used with dynamic offsets (accessed by multiple descriptors)
		static auto uniformBuffer(VkDeviceSize size, uint32_t instanceCount = MAX_FRAMES_IN_FLIGHT) -> Buffer::CreateInfo
		{
			AGX_ASSERT_X(size > 0, "Cannot create uniform buffer of size 0");
			AGX_ASSERT_X(instanceCount > 0, "Cannot create uniform buffer with 0 instances");
			return Buffer::CreateInfo{
				.instanceSize = size,
				.instanceCount = instanceCount,
				.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.allocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
				.minOffsetAlignment = VulkanContext::device().properties().limits.minUniformBufferOffsetAlignment
			};
		}

		static auto storageBuffer(VkDeviceSize size, uint32_t instanceCount = 1) -> Buffer::CreateInfo
		{
			AGX_ASSERT_X(size > 0, "Cannot create storage buffer of size 0");
			AGX_ASSERT_X(instanceCount > 0, "Cannot create storage buffer with 0 instances");
			return Buffer::CreateInfo{
				.instanceSize = size,
				.instanceCount = instanceCount,
				.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				.minOffsetAlignment = VulkanContext::device().properties().limits.minStorageBufferOffsetAlignment
			};
		}

		static auto vertexBuffer(VkDeviceSize size, uint32_t instanceCount = 1, VkBufferUsageFlags otherUsage = 0) -> Buffer::CreateInfo
		{
			AGX_ASSERT_X(size > 0, "Cannot create vertex buffer of size 0");
			AGX_ASSERT_X(instanceCount > 0, "Cannot create vertex buffer with 0 instances");
			return Buffer::CreateInfo{
				.instanceSize = size,
				.instanceCount = instanceCount,
				.usage = otherUsage | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			};
		}

		static auto indexBuffer(VkDeviceSize size, uint32_t instanceCount = 1, VkBufferUsageFlags otherUsage = 0) -> Buffer::CreateInfo
		{
			AGX_ASSERT_X(size > 0, "Cannot create index buffer of size 0");
			AGX_ASSERT_X(instanceCount > 0, "Cannot create index buffer with 0 instances");
			return Buffer::CreateInfo{
				.instanceSize = size,
				.instanceCount = instanceCount,
				.usage = otherUsage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			};
		}

		static auto stagingBuffer(VkDeviceSize size, uint32_t instanceCount = 1, VkBufferUsageFlags otherUsage = 0) -> Buffer::CreateInfo
		{
			AGX_ASSERT_X(size > 0, "Cannot create staging buffer of size 0");
			return Buffer::CreateInfo{
				.instanceSize = size,
				.instanceCount = instanceCount,
				.usage = otherUsage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				.allocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			};
		}

		Buffer() = default;
		explicit Buffer(const CreateInfo& info) :
			m_instanceSize{ info.instanceSize },
			m_instanceCount{ info.instanceCount },
			m_usage{ info.usage }
		{
			AGX_ASSERT_X(m_instanceSize > 0, "Cannot create buffer with instance size 0");
			AGX_ASSERT_X(m_instanceCount > 0, "Cannot create buffer with instance count 0");

			m_alignmentSize = computeAlignment(info.instanceSize, info.minOffsetAlignment);
			m_bufferSize = m_alignmentSize * m_instanceCount;
			VulkanContext::device().createBuffer(m_buffer, m_allocation, m_bufferSize, m_usage, info.allocFlags, VMA_MEMORY_USAGE_AUTO);

			VmaAllocationInfo allocInfo;
			vmaGetAllocationInfo(VulkanContext::device().allocator(), m_allocation, &allocInfo);
			m_mapped = allocInfo.pMappedData;
		}

		Buffer(const Buffer&) = delete;
		Buffer(Buffer&& other) noexcept
		{
			moveFrom(std::move(other));
		}

		~Buffer()
		{
			destroy();
		}

		auto operator=(const Buffer&) -> Buffer & = delete;
		auto operator=(Buffer&& other) noexcept -> Buffer&
		{
			if (this != &other)
			{
				destroy();
				moveFrom(std::move(other));
			}
			return *this;
		}

		operator VkBuffer() const { return m_buffer; }

		[[nodiscard]] auto buffer() const -> VkBuffer { return m_buffer; }
		[[nodiscard]] auto bufferSize() const -> VkDeviceSize { return m_bufferSize; }
		[[nodiscard]] auto instanceSize() const -> VkDeviceSize { return m_instanceSize; }
		[[nodiscard]] auto alignmentSize() const -> VkDeviceSize { return m_alignmentSize; }
		[[nodiscard]] auto instanceCount() const -> uint32_t { return m_instanceCount; }
		[[nodiscard]] auto usage() const -> VkBufferUsageFlags { return m_usage; }
		[[nodiscard]] auto isMapped() const -> bool { return m_mapped != nullptr; }
		[[nodiscard]] auto descriptorBufferInfo(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0) const -> VkDescriptorBufferInfo
		{
			AGX_ASSERT_X((size == VK_WHOLE_SIZE && offset == 0) || (offset + size <= m_bufferSize),
				"Requested descriptor buffer info exceeds buffer size");
			return VkDescriptorBufferInfo{ m_buffer, offset, size };
		}

		[[nodiscard]] auto descriptorBufferInfoFor(uint32_t index) const -> VkDescriptorBufferInfo
		{
			AGX_ASSERT_X(index < m_instanceCount, "Requested descriptor buffer info index exceeds instance count");
			return descriptorBufferInfo(m_alignmentSize, index * m_alignmentSize);
		}

		/// @brief Map the buffer memory to allow writing to it 
		/// @note Consider using persistent mapped memory to avoid repeated map/unmap calls
		void map()
		{
			AGX_ASSERT_X(!m_mapped, "Buffer is already mapped");
			VK_CHECK(vmaMapMemory(VulkanContext::device().allocator(), m_allocation, &m_mapped));
		}

		/// @brief Unmap the buffer memory
		/// @note Consider using persistent mapped memory to avoid repeated map/unmap calls
		void unmap()
		{
			if (m_mapped)
			{
				vmaUnmapMemory(VulkanContext::device().allocator(), m_allocation);
				m_mapped = nullptr;
			}
		}

		// TODO: Rework these write functions to have:
		// Better naming (pretty inconsistent right now)
		// Unified behavior (some map/unmap internally, some don't)
		// Too many overloads (some with size/offset, some without)
		// Better utilize templates for type safety
		// Maybe offer casted pointer to data for easier writing?

		/// @brief Only writes data to the buffer
		/// @note Buffer MUST be mapped before calling
		void write(const void* data)
		{
			write(data, m_bufferSize, 0);
		}

		void write(const void* data, VkDeviceSize size, VkDeviceSize offset)
		{
			AGX_ASSERT_X(data, "Data pointer is null");
			AGX_ASSERT_X(m_mapped, "Called write on buffer before map");

			memcpy(static_cast<uint8_t*>(m_mapped) + offset, data, size);
			flush(size, offset);
		}

		/// @brief Writes data of 'instanceSize' to the buffer at an offset of 'index * alignmentSize'
		/// @note Buffer MUST be mapped before calling
		void writeToIndex(const void* data, uint32_t index)
		{
			AGX_ASSERT_X(data, "Data pointer is null");
			AGX_ASSERT_X(m_mapped, "Called write on buffer before map");
			AGX_ASSERT_X(index < m_instanceCount, "Requested write index exceeds instance count");

			memcpy(static_cast<uint8_t*>(m_mapped) + (index * m_alignmentSize), data, m_instanceSize);
			flushIndex(index);
		}

		/// @brief Maps, writes data, then unmaps the buffer
		void singleWrite(const void* data)
		{
			AGX_ASSERT_X(data, "Data pointer is null");
			if (m_instanceCount == 1)
			{
				singleWrite(data, m_instanceSize, 0);
			}
			else // Copy data to all instances
			{
				map();
				for (uint32_t i = 0; i < m_instanceCount; i++)
				{
					memcpy(static_cast<uint8_t*>(m_mapped) + (i * m_alignmentSize), data, m_instanceSize);
				}
				flush(m_bufferSize, 0);
				unmap();
			}
		}

		void singleWrite(const void* data, VkDeviceSize size, VkDeviceSize offset)
		{
			AGX_ASSERT_X(data, "Data pointer is null");
			AGX_ASSERT_X((size == VK_WHOLE_SIZE && offset == 0) || (offset + size <= m_bufferSize),
				"Single write exceeds buffer size");
			VK_CHECK(vmaCopyMemoryToAllocation(VulkanContext::device().allocator(), data, m_allocation, offset, size));
		}


		/// @brief Flushes the buffer memory to make it visible to the device 
		/// @note Only required for non-coherent memory, 'write' functions already flush
		void flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
		{
			// TODO: Avoid flushing if memory is VK_MEMORY_PROPERTY_HOST_COHERENT_BIT (redundant)
			AGX_ASSERT_X(m_mapped, "Called flush on buffer before map");
			AGX_ASSERT_X((size == VK_WHOLE_SIZE && offset == 0) || (offset + size <= m_bufferSize),
				"Flush range exceeds buffer size");
			VK_CHECK(vmaFlushAllocation(VulkanContext::device().allocator(), m_allocation, offset, size));
		}

		/// @brief Flush the memory range at 'index * alignmentSize'
		void flushIndex(uint32_t index)
		{
			AGX_ASSERT_X(index < m_instanceCount, "Requested flush index exceeds instance count");
			flush(m_alignmentSize, index * m_alignmentSize);
		}

		/// @brief Uploads data to the buffer using a staging buffer (Used for device local memory)
		void upload(const void* data, VkDeviceSize size)
		{
			// TODO: Use a global shared staging buffer (creating a new one for each upload is inefficient)
			Buffer stagingBuffer{ Buffer::stagingBuffer(size) };
			stagingBuffer.singleWrite(data, size, 0);
			stagingBuffer.copyTo(*this, size);
		}

		/// @brief Copy data into the mapped buffer at an offset of 'index * alignmentSize'
		void copy(const void* data, VkDeviceSize size, uint32_t index = 0)
		{
			AGX_ASSERT_X(data, "Data pointer is null");
			AGX_ASSERT_X(m_mapped, "Called copy on buffer before map");
			AGX_ASSERT_X(size <= m_bufferSize, "Copy size exceeds buffer size");
			AGX_ASSERT_X(index < m_instanceCount, "Requested copy index exceeds instance count");
			VK_CHECK(vmaCopyMemoryToAllocation(VulkanContext::device().allocator(), data, m_allocation,
				index * m_alignmentSize, size));
		}

		/// @brief Copy the buffer to another buffer
		void copyTo(Buffer& dest, VkDeviceSize size)
		{
			AGX_ASSERT_X((size == VK_WHOLE_SIZE) || (size <= m_bufferSize && size <= dest.m_bufferSize),
				"Copy size exceeds source or destination buffer size");
			VulkanContext::device().copyBuffer(m_buffer, dest.m_buffer, size);
		}

		void copyTo(VkCommandBuffer cmd, Buffer& dest, uint32_t srcIndex, uint32_t destIndex) const
		{
			AGX_ASSERT_X(srcIndex < m_instanceCount, "Source index exceeds instance count");
			AGX_ASSERT_X(destIndex < dest.m_instanceCount, "Destination index exceeds instance count");

			VkBufferCopy copy{
				.srcOffset = srcIndex * m_alignmentSize,
				.dstOffset = destIndex * dest.m_alignmentSize,
				.size = m_instanceSize
			};
			vkCmdCopyBuffer(cmd, m_buffer, dest.m_buffer, 1, &copy);
		}

		template<typename T>
		void upload(const std::vector<T>& data)
		{
			AGX_ASSERT_X(!data.empty(), "Data vector is empty");
			AGX_ASSERT_X(sizeof(T) * data.size() <= m_bufferSize, "Data size exceeds buffer size");
			upload(data.data(), sizeof(T) * data.size());
		}

		template<typename T>
		void copy(const std::vector<T>& src, uint32_t index = 0)
		{
			if (src.empty())
				return;

			AGX_ASSERT_X(sizeof(T) * src.size() <= m_instanceSize, "Source data size exceeds buffer size");
			copy(src.data(), sizeof(T) * src.size(), index);
		}

		template<typename T = void>
		auto data(uint32_t index = 0) -> T*
		{
			AGX_ASSERT_X(m_mapped, "Called mappedAs on buffer before map");
			AGX_ASSERT_X(sizeof(T) <= m_instanceSize, "Mapped type size exceeds instance size");
			AGX_ASSERT_X(index < m_instanceCount, "Mapped index exceeds instance count");
			return reinterpret_cast<T*>(static_cast<uint8_t*>(m_mapped) + index * m_alignmentSize);
		}

	private:
		static auto computeAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) -> VkDeviceSize
		{
			if (minOffsetAlignment > 0)
				return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);

			return instanceSize;
		}

		void destroy()
		{
			VulkanContext::destroy(m_buffer, m_allocation);
			m_buffer = VK_NULL_HANDLE;
			m_allocation = VK_NULL_HANDLE;
		}

		void moveFrom(Buffer&& other)
		{
			m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
			m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
			m_bufferSize = std::exchange(other.m_bufferSize, 0);
			m_instanceSize = std::exchange(other.m_instanceSize, 0);
			m_alignmentSize = std::exchange(other.m_alignmentSize, 0);
			m_instanceCount = std::exchange(other.m_instanceCount, 0);
			m_usage = std::exchange(other.m_usage, 0);
			m_mapped = std::exchange(other.m_mapped, nullptr);
		}

		VkBuffer m_buffer{ VK_NULL_HANDLE };
		VmaAllocation m_allocation{ VK_NULL_HANDLE };
		VkDeviceSize m_bufferSize{ 0 };
		VkDeviceSize m_alignmentSize{ 0 };
		VkDeviceSize m_instanceSize{ 0 };
		uint32_t m_instanceCount{ 0 };
		VkBufferUsageFlags m_usage{ 0 };
		void* m_mapped{ nullptr };
	};
}

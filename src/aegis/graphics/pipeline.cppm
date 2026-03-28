module;

#include "graphics/vulkan/volk_include.h"

export module Aegis.Graphics.Pipeline;

export namespace Aegis::Graphics
{
	class Pipeline
	{
	public:
		enum class Flags
		{
			None = 0,
			MeshShader = 1 << 0,
		};

		struct LayoutConfig
		{
			std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
			std::vector<VkPushConstantRange> pushConstantRanges;
		};

		struct GraphicsConfig
		{
			GraphicsConfig() = default;
			GraphicsConfig(const GraphicsConfig&) = delete;
			GraphicsConfig& operator=(const GraphicsConfig&) = delete;

			std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
			std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
			std::vector<VkPipelineShaderStageCreateInfo> shaderStges{};
			std::vector<VkFormat> colorAttachmentFormats{};
			VkPipelineRenderingCreateInfo renderingInfo{};
			VkPipelineViewportStateCreateInfo viewportInfo{};
			VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
			VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
			VkPipelineMultisampleStateCreateInfo multisampleInfo{};
			std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments{};
			VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
			VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
			std::vector<VkDynamicState> dynamicStateEnables{};
			VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
			uint32_t subpass = 0;
			Flags flags = Flags::None;
		};

		class GraphicsBuilder
		{
		public:
			GraphicsBuilder()
			{
				Pipeline::defaultGraphicsPipelineConfig(m_graphicsConfig);
			}

			~GraphicsBuilder()
			{
				for (const auto& module : m_shaderModules)
				{
					vkDestroyShaderModule(VulkanContext::device(), module, nullptr);
				}
			}

			auto addDescriptorSetLayout(VkDescriptorSetLayout descriptorSetLayout) -> GraphicsBuilder&
			{
				m_layoutConfig.descriptorSetLayouts.push_back(descriptorSetLayout);
				return *this;
			}

			auto addPushConstantRange(VkShaderStageFlags stageFlags, uint32_t size, uint32_t offset = 0) -> GraphicsBuilder&
			{
				VkPushConstantRange pushConstantRange{};
				pushConstantRange.stageFlags = stageFlags;
				pushConstantRange.size = size;
				pushConstantRange.offset = offset;
				m_layoutConfig.pushConstantRanges.emplace_back(pushConstantRange);
				return *this;
			}

			auto addShaderStage(VkShaderStageFlagBits stage, const std::filesystem::path& shaderPath) -> GraphicsBuilder&
			{
				VkShaderModule shaderModule = Tools::createShaderModule(VulkanContext::device(), shaderPath);
				m_shaderModules.emplace_back(shaderModule);
				addShaderStage(stage, shaderModule, "main");
				return *this;
			}

			auto addShaderStages(VkShaderStageFlags stages, const std::filesystem::path& shaderPath) -> GraphicsBuilder&
			{
				VkShaderModule shaderModule = Tools::createShaderModule(VulkanContext::device(), shaderPath);
				m_shaderModules.emplace_back(shaderModule);

				if (stages & VK_SHADER_STAGE_VERTEX_BIT)
					addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, shaderModule, "vertexMain");
				if (stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
					addShaderStage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, shaderModule, "tessControlMain");
				if (stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
					addShaderStage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, shaderModule, "tessEvalMain");
				if (stages & VK_SHADER_STAGE_GEOMETRY_BIT)
					addShaderStage(VK_SHADER_STAGE_GEOMETRY_BIT, shaderModule, "geometryMain");
				if (stages & VK_SHADER_STAGE_FRAGMENT_BIT)
					addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, shaderModule, "fragmentMain");
				if (stages & VK_SHADER_STAGE_COMPUTE_BIT)
					addShaderStage(VK_SHADER_STAGE_COMPUTE_BIT, shaderModule, "computeMain");
				if (stages & VK_SHADER_STAGE_TASK_BIT_EXT)
					addShaderStage(VK_SHADER_STAGE_TASK_BIT_EXT, shaderModule, "taskMain");
				if (stages & VK_SHADER_STAGE_MESH_BIT_EXT)
					addShaderStage(VK_SHADER_STAGE_MESH_BIT_EXT, shaderModule, "meshMain");
				return *this;
			}

			auto addColorAttachment(VkFormat colorFormat, bool alphaBlending = false) -> GraphicsBuilder&
			{
				m_graphicsConfig.colorAttachmentFormats.emplace_back(colorFormat);
				m_graphicsConfig.renderingInfo.colorAttachmentCount = static_cast<uint32_t>(m_graphicsConfig.colorAttachmentFormats.size());
				m_graphicsConfig.renderingInfo.pColorAttachmentFormats = m_graphicsConfig.colorAttachmentFormats.data();

				VkPipelineColorBlendAttachmentState colorBlendAttachment{};
				colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				colorBlendAttachment.blendEnable = VK_FALSE;
				colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

				if (alphaBlending)
				{
					colorBlendAttachment.blendEnable = VK_TRUE;
					colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				}

				m_graphicsConfig.colorBlendAttachments.emplace_back(colorBlendAttachment);

				m_graphicsConfig.colorBlendInfo.attachmentCount = m_graphicsConfig.renderingInfo.colorAttachmentCount;
				m_graphicsConfig.colorBlendInfo.pAttachments = m_graphicsConfig.colorBlendAttachments.data();

				return *this;
			}

			auto setDepthAttachment(VkFormat depthFormat) -> GraphicsBuilder&
			{
				m_graphicsConfig.renderingInfo.depthAttachmentFormat = depthFormat;
				return *this;
			}

			auto setStencilFormat(VkFormat stencilFormat) -> GraphicsBuilder&
			{
				m_graphicsConfig.renderingInfo.stencilAttachmentFormat = stencilFormat;
				return *this;
			}

			auto setDepthTest(bool enableDepthTest, bool writeDepth, VkCompareOp compareOp = VK_COMPARE_OP_LESS) -> GraphicsBuilder&
			{
				m_graphicsConfig.depthStencilInfo.depthTestEnable = enableDepthTest;
				m_graphicsConfig.depthStencilInfo.depthWriteEnable = writeDepth;
				m_graphicsConfig.depthStencilInfo.depthCompareOp = compareOp;
				return *this;
			}

			auto setCullMode(VkCullModeFlags cullMode) -> GraphicsBuilder&
			{
				m_graphicsConfig.rasterizationInfo.cullMode = cullMode;
				return *this;
			}

			auto setVertexBindingDescriptions(const std::vector<VkVertexInputBindingDescription>& bindingDescriptions) -> GraphicsBuilder&
			{
				m_graphicsConfig.bindingDescriptions = bindingDescriptions;
				return *this;
			}

			auto setVertexAttributeDescriptions(const std::vector<VkVertexInputAttributeDescription>& attributeDescriptions) -> GraphicsBuilder&
			{
				m_graphicsConfig.attributeDescriptions = attributeDescriptions;
				return *this;
			}

			auto addFlag(Flags flag) -> GraphicsBuilder&
			{
				m_graphicsConfig.flags = static_cast<Flags>(static_cast<uint32_t>(m_graphicsConfig.flags) | static_cast<uint32_t>(flag));
				return *this;
			}

			auto buildUnique() -> std::unique_ptr<Pipeline>
			{
				return std::make_unique<Pipeline>(m_layoutConfig, m_graphicsConfig);
			}

			auto build() -> Pipeline
			{
				return Pipeline{ m_layoutConfig, m_graphicsConfig };
			}

		private:
			void addShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule, const char* entryPoint)
			{
				m_graphicsConfig.shaderStges.emplace_back(VkPipelineShaderStageCreateInfo{
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = stage,
					.module = shaderModule,
					.pName = entryPoint,
					});
			}

			LayoutConfig m_layoutConfig;
			GraphicsConfig m_graphicsConfig;
			std::vector<VkShaderModule> m_shaderModules;
		};

		struct ComputeConfig
		{
			VkPipelineShaderStageCreateInfo shaderStage;
		};

		class ComputeBuilder
		{
		public:
			ComputeBuilder() = default;
			~ComputeBuilder()
			{
				if (m_computeConfig.shaderStage.module)
				{
					vkDestroyShaderModule(VulkanContext::device(), m_computeConfig.shaderStage.module, nullptr);
				}
			}

			auto addDescriptorSetLayout(VkDescriptorSetLayout descriptorSetLayout) -> ComputeBuilder&
			{
				m_layoutConfig.descriptorSetLayouts.emplace_back(descriptorSetLayout);
				return *this;
			}

			auto addPushConstantRange(VkShaderStageFlags stageFlags, uint32_t size) -> ComputeBuilder&
			{
				AGX_ASSERT_X(size <= VulkanContext::device().properties().limits.maxPushConstantsSize, "Push constant size exceeds device limits");

				VkPushConstantRange pushConstantRange{};
				pushConstantRange.stageFlags = stageFlags;
				pushConstantRange.size = size;
				pushConstantRange.offset = 0;
				m_layoutConfig.pushConstantRanges.emplace_back(pushConstantRange);
				return *this;
			}

			auto setShaderStage(const std::filesystem::path& shaderPath, const char* entry = "main") -> ComputeBuilder&
			{
				VkShaderModule shaderModule = Tools::createShaderModule(VulkanContext::device(), shaderPath);
				m_computeConfig.shaderStage = Tools::createShaderStage(VK_SHADER_STAGE_COMPUTE_BIT, shaderModule, entry);
				return *this;
			}

			auto buildUnique() -> std::unique_ptr<Pipeline>
			{
				return std::make_unique<Pipeline>(m_layoutConfig, m_computeConfig);
			}

			auto build() -> Pipeline
			{
				return Pipeline{ m_layoutConfig, m_computeConfig };
			}

		private:
			LayoutConfig m_layoutConfig;
			ComputeConfig m_computeConfig;
		};

		Pipeline() = default;
		Pipeline(const LayoutConfig& layoutConfig, const GraphicsConfig& graphicsConfig)
			: m_bindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS }, m_flags{ graphicsConfig.flags }
		{
			createPipelineLayout(layoutConfig);
			createGraphicsPipeline(graphicsConfig);
		}

		Pipeline(const LayoutConfig& layoutConfig, const ComputeConfig& computeConfig)
			: m_bindPoint{ VK_PIPELINE_BIND_POINT_COMPUTE }
		{
			createPipelineLayout(layoutConfig);
			createComputePipeline(computeConfig);
		}

		Pipeline(const Pipeline&) = delete;
		Pipeline(Pipeline&& other) noexcept
			: m_layout{ other.m_layout }, m_pipeline{ other.m_pipeline }, m_bindPoint{ other.m_bindPoint }, m_flags{ other.m_flags }
		{
			other.m_layout = VK_NULL_HANDLE;
			other.m_pipeline = VK_NULL_HANDLE;
		}

		~Pipeline()
		{
			destroy();
		}

		auto operator=(const Pipeline&) -> Pipeline & = delete;
		auto operator=(Pipeline&& other) noexcept -> Pipeline&
		{
			if (this != &other)
			{
				destroy();
				m_layout = other.m_layout;
				m_pipeline = other.m_pipeline;
				m_bindPoint = other.m_bindPoint;
				m_flags = other.m_flags;
				other.m_layout = VK_NULL_HANDLE;
				other.m_pipeline = VK_NULL_HANDLE;
			}
			return *this;
		}

		operator VkPipeline() const { return m_pipeline; }

		[[nodiscard]] auto pipeline() const -> VkPipeline { return m_pipeline; }
		[[nodiscard]] auto layout() const -> VkPipelineLayout { return m_layout; }
		[[nodiscard]] auto bindPoint() const -> VkPipelineBindPoint { return m_bindPoint; }
		[[nodiscard]] auto hasFlag(Flags flag) const -> bool
		{
			return (static_cast<uint32_t>(m_flags) & static_cast<uint32_t>(flag)) != 0;
		}

		void bind(VkCommandBuffer commandBuffer) const
		{
			vkCmdBindPipeline(commandBuffer, m_bindPoint, m_pipeline);
		}

		void bindDescriptorSet(VkCommandBuffer cmd, uint32_t setIndex, VkDescriptorSet descriptorSet) const
		{
			vkCmdBindDescriptorSets(cmd, m_bindPoint, m_layout, setIndex, 1, &descriptorSet, 0, nullptr);
		}

		void pushConstants(VkCommandBuffer cmd, VkShaderStageFlags stageFlags, const void* data, uint32_t size, uint32_t offset = 0) const
		{
			vkCmdPushConstants(cmd, m_layout, stageFlags, offset, size, data);
		}

		template<typename T>
		void pushConstants(VkCommandBuffer cmd, VkShaderStageFlags stageFlags, const T& data) const
		{
			pushConstants(cmd, stageFlags, &data, sizeof(T));
		}

		static void defaultGraphicsPipelineConfig(Pipeline::GraphicsConfig& configInfo)
		{
			configInfo.renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

			configInfo.inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

			configInfo.viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			configInfo.viewportInfo.viewportCount = 1;
			configInfo.viewportInfo.pViewports = nullptr;
			configInfo.viewportInfo.scissorCount = 1;
			configInfo.viewportInfo.pScissors = nullptr;

			configInfo.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			configInfo.rasterizationInfo.depthClampEnable = VK_FALSE;
			configInfo.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
			configInfo.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
			configInfo.rasterizationInfo.lineWidth = 1.0f;
			configInfo.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
			configInfo.rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			configInfo.rasterizationInfo.depthBiasEnable = VK_FALSE;
			configInfo.rasterizationInfo.depthBiasConstantFactor = 0.0f;
			configInfo.rasterizationInfo.depthBiasClamp = 0.0f;
			configInfo.rasterizationInfo.depthBiasSlopeFactor = 0.0f;

			configInfo.multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			configInfo.multisampleInfo.sampleShadingEnable = VK_FALSE;
			configInfo.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			configInfo.multisampleInfo.minSampleShading = 1.0f;
			configInfo.multisampleInfo.pSampleMask = nullptr;
			configInfo.multisampleInfo.alphaToCoverageEnable = VK_FALSE;
			configInfo.multisampleInfo.alphaToOneEnable = VK_FALSE;

			configInfo.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			configInfo.colorBlendInfo.logicOpEnable = VK_FALSE;
			configInfo.colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;
			configInfo.colorBlendInfo.attachmentCount = static_cast<uint32_t>(configInfo.colorBlendAttachments.size());
			configInfo.colorBlendInfo.pAttachments = configInfo.colorBlendAttachments.data();
			configInfo.colorBlendInfo.blendConstants[0] = 0.0f;
			configInfo.colorBlendInfo.blendConstants[1] = 0.0f;
			configInfo.colorBlendInfo.blendConstants[2] = 0.0f;
			configInfo.colorBlendInfo.blendConstants[3] = 0.0f;

			configInfo.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
			configInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
			configInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
			configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
			configInfo.depthStencilInfo.minDepthBounds = 0.0f;
			configInfo.depthStencilInfo.maxDepthBounds = 1.0f;
			configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;
			configInfo.depthStencilInfo.front = {};
			configInfo.depthStencilInfo.back = {};

			configInfo.dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			configInfo.dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
			configInfo.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
			configInfo.dynamicStateInfo.flags = 0;

			configInfo.bindingDescriptions = { StaticMesh::bindingDescription() };
			configInfo.attributeDescriptions = StaticMesh::attributeDescriptions();
		}

	private:
		void createPipelineLayout(const LayoutConfig& config)
		{
			VkPipelineLayoutCreateInfo layoutInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size()),
				.pSetLayouts = config.descriptorSetLayouts.data(),
				.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstantRanges.size()),
				.pPushConstantRanges = config.pushConstantRanges.data(),
			};

			VK_CHECK(vkCreatePipelineLayout(VulkanContext::device(), &layoutInfo, nullptr, &m_layout));
		}

		void createGraphicsPipeline(const GraphicsConfig& config)
		{
			VkPipelineVertexInputStateCreateInfo vertexInputInfo{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
				.vertexBindingDescriptionCount = static_cast<uint32_t>(config.bindingDescriptions.size()),
				.pVertexBindingDescriptions = config.bindingDescriptions.data(),
				.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.attributeDescriptions.size()),
				.pVertexAttributeDescriptions = config.attributeDescriptions.data(),
			};

			VkGraphicsPipelineCreateInfo pipelineInfo{
				.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
				.pNext = &config.renderingInfo,
				.stageCount = static_cast<uint32_t>(config.shaderStges.size()),
				.pStages = config.shaderStges.data(),
				.pVertexInputState = hasFlag(Flags::MeshShader) ? nullptr : &vertexInputInfo,
				.pInputAssemblyState = hasFlag(Flags::MeshShader) ? nullptr : &config.inputAssemblyInfo,
				.pTessellationState = nullptr,
				.pViewportState = &config.viewportInfo,
				.pRasterizationState = &config.rasterizationInfo,
				.pMultisampleState = &config.multisampleInfo,
				.pDepthStencilState = &config.depthStencilInfo,
				.pColorBlendState = &config.colorBlendInfo,
				.pDynamicState = &config.dynamicStateInfo,
				.layout = m_layout,
				.renderPass = VK_NULL_HANDLE,
				.subpass = config.subpass,
				.basePipelineHandle = VK_NULL_HANDLE,
				.basePipelineIndex = -1,
			};

			VK_CHECK(vkCreateGraphicsPipelines(VulkanContext::device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));
		}

		void createComputePipeline(const ComputeConfig& config)
		{
			AGX_ASSERT_X(config.shaderStage.module != VK_NULL_HANDLE, "Cannot create pipeline: no shader provided");

			VkComputePipelineCreateInfo pipelineInfo{
				.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.stage = config.shaderStage,
				.layout = m_layout,
				.basePipelineHandle = VK_NULL_HANDLE,
				.basePipelineIndex = -1,
			};

			VK_CHECK(vkCreateComputePipelines(VulkanContext::device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));
		}

		void destroy()
		{
			VulkanContext::destroy(m_pipeline);
			m_pipeline = VK_NULL_HANDLE;

			VulkanContext::destroy(m_layout);
			m_layout = VK_NULL_HANDLE;
		}

		VkPipelineLayout m_layout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkPipelineBindPoint m_bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		Flags m_flags = Flags::None;
	};
}

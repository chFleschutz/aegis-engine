module;
#include <memory>

export module aegis.rhi.vulkan:device;
import :core;
import aegis.rhi;

export namespace aegis::rhi::vulkan
{
class Device final : public rhi::Device
{
public:
    explicit Device(const rhi::Device::Desc& desc);

    auto createBuffer(const Buffer::Desc& desc) -> std::unique_ptr<Buffer> override;
    auto createTexture(const Buffer::Desc& desc) -> std::unique_ptr<Texture> override;
    auto createPipeline(const Buffer::Desc& desc) -> std::unique_ptr<Pipeline> override;
    auto createCommandBuffer(const CommandBuffer::Desc& desc) -> std::unique_ptr<CommandBuffer> override;

    void submit(const CommandBuffer& cmd) override;

    void beginFrame() override;
    void endFrame() override;

    void waitIdle() override;

private:
    void createInstance();

    vk::raii::Context m_context;
    vk::raii::Instance m_instance{ nullptr };
};
}

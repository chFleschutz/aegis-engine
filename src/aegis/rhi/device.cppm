module;
#include <memory>
#include <string>

export module aegis.rhi:device;
import :buffer;
import :texture;
import :pipeline;
import :command_buffer;

import vulkan_hpp;

export namespace aegis::rhi
{
class Device
{
public:
    struct Desc
    {
        std::string appName;
    };

    explicit Device(Desc desc);
    ~Device() = default;

    // auto createBuffer(const Buffer::Desc& desc) -> std::unique_ptr<Buffer>;
    // auto createTexture(const Buffer::Desc& desc) -> std::unique_ptr<Texture>;
    // auto createPipeline(const Buffer::Desc& desc) -> std::unique_ptr<Pipeline>;
    // auto createCommandBuffer(const CommandBuffer::Desc& desc) -> std::unique_ptr<CommandBuffer>;
    //
    // void submit(const CommandBuffer& cmd);
    //
    // void beginFrame();
    // void endFrame();
    //
    // void waitIdle();

private:
    void createInstance();

    vk::raii::Context m_context;
    vk::raii::Instance m_instance{ nullptr };
};
}

module;
#include <memory>
#include <string>

export module aegis.rhi:device;
import :buffer;
import :texture;
import :pipeline;
import :command_buffer;

export namespace aegis::rhi
{
class Device
{
public:
    struct Desc
    {
        std::string appName;
    };

    virtual ~Device() = default;

    virtual auto createBuffer(const Buffer::Desc& desc) -> std::unique_ptr<Buffer> = 0;
    virtual auto createTexture(const Buffer::Desc& desc) -> std::unique_ptr<Texture> = 0;
    virtual auto createPipeline(const Buffer::Desc& desc) -> std::unique_ptr<Pipeline> = 0;
    virtual auto createCommandBuffer(const CommandBuffer::Desc& desc) -> std::unique_ptr<CommandBuffer> = 0;

    virtual void submit(const CommandBuffer& cmd) = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    virtual void waitIdle() = 0;
};
}

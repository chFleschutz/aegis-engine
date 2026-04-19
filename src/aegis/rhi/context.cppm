module;

#include <cstdint>
#include <memory>

export module aegis.rhi:context;

import :device;

export namespace aegis::rhi
{
class Context
{
public:
    Context() = default;
    ~Context() = default;

    [[nodiscard]] auto device() const -> Device& { return *m_device; }

    void resize(uint32_t width, uint32_t height) {}

private:
    std::unique_ptr<Device> m_device;
    uint32_t m_frameIndex{ 0 };
};
}

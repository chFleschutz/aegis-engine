#include <doctest/doctest.h>

import std;
import aegis.render_graph;
import aegis.rhi;

namespace
{
using namespace aegis::render_graph;
using namespace aegis::render_graph::detail;

constexpr auto allUsages = std::to_array({
    Usage::ColorAttachment,
    Usage::DepthAttachment,
    Usage::DepthRead,
    Usage::SampledFragment,
    Usage::SampledCompute,
    Usage::StorageRead,
    Usage::StorageWrite,
    Usage::ShaderRead,
    Usage::IndirectRead,
    Usage::TransferSrc,
    Usage::TransferDst,
    Usage::HostWrite,
});

constexpr auto isImageUsage(Usage usage) -> bool
{
    return layoutClass(usage) != LayoutClass::None;
}
}

TEST_SUITE("render_graph::Usage")
{
    TEST_CASE("every write usage maps to a writing state and every image usage has a layout class")
    {
        for (const auto usage : allUsages)
        {
            CAPTURE(static_cast<int>(usage));

            if (usage == Usage::HostWrite)
            {
                CHECK(isWrite(usage));
                CHECK(toResourceState(usage) == aegis::rhi::ResourceState::Unknown);
                continue;
            }

            CHECK(toResourceState(usage) != aegis::rhi::ResourceState::Unknown);

            // Only writes may leave a read-only state: a read usage never maps to a writing state.
            const auto state = toResourceState(usage);
            const bool writingState = state == aegis::rhi::ResourceState::ColorAttachment || state == aegis::rhi::ResourceState::DepthWrite || state == aegis::rhi::ResourceState::CopyDst || (state == aegis::rhi::ResourceState::ComputeStorage && usage == Usage::StorageWrite);
            CHECK(isWrite(usage) == writingState);

            // An image usage has a layout class and image usage flags; a buffer-only usage has neither.
            const bool imageFlags = toImageUsage(usage) != aegis::rhi::ImageUsage::None;
            CHECK(isImageUsage(usage) == imageFlags);
        }
    }
}

TEST_SUITE("render_graph::Usage")
{
    TEST_CASE("every usage fits images or buffers, and the image usages are the ones with a layout")
    {
        for (const auto usage : allUsages)
        {
            CAPTURE(static_cast<int>(usage));
            CHECK((supportsImage(usage) || supportsBuffer(usage)));
            CHECK(supportsImage(usage) == isImageUsage(usage));
            CHECK_FALSE(usageName(usage).empty());
        }
    }

    TEST_CASE("images take attachments and samples, buffers take device-address and indirect reads")
    {
        CHECK(supportsImage(Usage::ColorAttachment));
        CHECK_FALSE(supportsBuffer(Usage::ColorAttachment));
        CHECK(supportsBuffer(Usage::ShaderRead));
        CHECK_FALSE(supportsImage(Usage::ShaderRead));
        CHECK(supportsImage(Usage::TransferSrc));
        CHECK(supportsBuffer(Usage::TransferSrc));
    }
}

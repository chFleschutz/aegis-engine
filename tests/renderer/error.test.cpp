#include <doctest/doctest.h>

import std;
import aegis.renderer;
import aegis.rhi;

using namespace aegis::renderer;

TEST_SUITE("renderer::Error")
{
    TEST_CASE("every error code has a distinct, non-empty name")
    {
        std::set<std::string_view> names;
        for (auto i = std::to_underlying(ErrorCode::RHIInitializationFailed);
            i <= std::to_underlying(ErrorCode::ResourceRecreateFailed);
            ++i)
        {
            auto name = errorCodeName(static_cast<ErrorCode>(i));
            CHECK(!name.empty());
            CHECK(name != "Unknown");
            CHECK(names.insert(name).second);
        }
    }

    TEST_CASE("makeError keeps the code and message and has no cause")
    {
        auto error = makeError(ErrorCode::FrameBeginFailed, "no image").error();
        CHECK(error.code == ErrorCode::FrameBeginFailed);
        CHECK(error.message == "no image");
        CHECK(!error.rhi.has_value());
    }

    TEST_CASE("makeError wraps an rhi error without changing it")
    {
        auto cause = aegis::rhi::makeError(aegis::rhi::ErrorCode::OutOfDeviceMemory).error();
        auto error = makeError(ErrorCode::ResourceRecreateFailed, "image 'Depth'", cause).error();
        CHECK(error.code == ErrorCode::ResourceRecreateFailed);
        REQUIRE(error.rhi.has_value());
        CHECK(error.rhi->code == aegis::rhi::ErrorCode::OutOfDeviceMemory);
    }

    TEST_CASE("describe prints the code name and the message")
    {
        auto error = makeError(ErrorCode::QueueSubmitFailed, "graphics queue").error();
        CHECK(describe(error) == "QueueSubmitFailed: graphics queue");
    }

    TEST_CASE("describe prints the code name alone without a message")
    {
        auto error = makeError(ErrorCode::SwapchainOutOfDate).error();
        CHECK(describe(error) == "SwapchainOutOfDate");
    }

    TEST_CASE("describe appends the rhi cause through rhi::describe")
    {
        auto cause = aegis::rhi::makeError(aegis::rhi::ErrorCode::DeviceLost).error();
        auto error = makeError(ErrorCode::SwapchainPresentFailed, "present", cause).error();
        CHECK(describe(error) == std::format("SwapchainPresentFailed: present (caused by {})", aegis::rhi::describe(cause)));
        CHECK(describe(error).contains("DeviceLost"));
    }
}

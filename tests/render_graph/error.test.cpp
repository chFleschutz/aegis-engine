#include <doctest/doctest.h>

import std;
import aegis.render_graph;
import aegis.rhi;

namespace
{
using namespace aegis::render_graph;
}

TEST_SUITE("render_graph::Error")
{
    TEST_CASE("errorCodeName names every code")
    {
        constexpr std::array codes{ ErrorCode::Unknown, ErrorCode::InvalidGraph, ErrorCode::LimitExceeded, ErrorCode::Unsupported,
            ErrorCode::ShaderLoadFailed, ErrorCode::ResourceCreationFailed, ErrorCode::SubmitFailed, ErrorCode::InitializationFailed,
            ErrorCode::RhiFailed };

        std::set<std::string_view> names;
        for (const auto code : codes)
        {
            CHECK(!errorCodeName(code).empty());
            names.insert(errorCodeName(code));
        }
        CHECK(names.size() == codes.size()); // Distinct, none fell through to "Unknown"
        CHECK(errorCodeName(ErrorCode::InvalidGraph) == "InvalidGraph");
    }

    TEST_CASE("makeError keeps the code and message")
    {
        const auto error = makeError(ErrorCode::LimitExceeded, "full").error();
        CHECK(error.code == ErrorCode::LimitExceeded);
        CHECK(error.message == "full");
        CHECK(!error.rhi);
    }

    TEST_CASE("makeError wrapping an rhi error copies the cause")
    {
        const auto wrapped = makeError(aegis::rhi::makeError(aegis::rhi::ErrorCode::OutOfDeviceMemory).error(), "no buffer").error();
        CHECK(wrapped.code == ErrorCode::RhiFailed);
        REQUIRE(wrapped.rhi.has_value());
        CHECK(wrapped.rhi->code == aegis::rhi::ErrorCode::OutOfDeviceMemory);

        const auto specific =
            makeError(ErrorCode::ShaderLoadFailed, "no pipeline", aegis::rhi::makeError(aegis::rhi::ErrorCode::InvalidArgument).error()).error();
        CHECK(specific.code == ErrorCode::ShaderLoadFailed);
        REQUIRE(specific.rhi.has_value());
        CHECK(specific.rhi->code == aegis::rhi::ErrorCode::InvalidArgument);
    }

    TEST_CASE("describe prints the code and message")
    {
        const auto error = makeError(ErrorCode::InvalidGraph, "cycle").error();
        CHECK(describe(error) == "InvalidGraph: cycle");
        CHECK(describe(makeError(ErrorCode::Unsupported, "").error()) == "Unsupported");
    }

    TEST_CASE("describe appends the rhi cause")
    {
        const auto cause = aegis::rhi::makeError(aegis::rhi::ErrorCode::DeviceLost).error();
        const auto error = makeError(cause, "Failed to create buffer").error();

        const auto text = describe(error);
        CHECK(text.starts_with("RhiFailed: Failed to create buffer (caused by "));
        CHECK(text.contains(aegis::rhi::describe(cause)));
        CHECK(text.contains("DeviceLost"));
    }
}

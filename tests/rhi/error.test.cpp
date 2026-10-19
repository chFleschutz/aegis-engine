#include <doctest/doctest.h>

import std;
import aegis.rhi;

using namespace aegis::rhi;

TEST_SUITE("rhi::Error")
{
    TEST_CASE("every error code has a distinct, non-empty name")
    {
        std::set<std::string_view> names;
        for (auto i = std::to_underlying(ErrorCode::Unknown); i <= std::to_underlying(ErrorCode::InvalidArgument); ++i)
        {
            auto name = errorCodeName(static_cast<ErrorCode>(i));
            CHECK(!name.empty());
            CHECK(names.insert(name).second);
        }
    }

    TEST_CASE("describe names the code")
    {
        auto error = makeError(ErrorCode::OutOfDate).error();
        auto text = describe(error);
        CHECK(text.starts_with("OutOfDate"));
    }

#ifndef NDEBUG
    TEST_CASE("describe names the source location in debug builds")
    {
        auto error = makeError(ErrorCode::NotReady).error();
        CHECK(describe(error).contains("error.test.cpp"));
    }
#endif
}

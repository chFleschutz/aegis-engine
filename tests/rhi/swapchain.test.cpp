#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

import aegis.rhi;

TEST_SUITE("rhi::swapchain policy")
{
    using aegis::rhi::Extent2D;
    using aegis::rhi::detail::chooseImageCount;
    using aegis::rhi::detail::chooseSwapchainExtent;

    // What a surface reports when it defers the size choice to the swapchain.
    constexpr Extent2D NoCurrentExtent{
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max()
    };

    TEST_CASE("a surface with a fixed extent wins over the preferred size")
    {
        auto extent = chooseSwapchainExtent(
            Extent2D{ 1920, 1080 },
            Extent2D{ 1, 1 },
            Extent2D{ 4096, 4096 },
            Extent2D{ 800, 600 });

        CHECK(extent == Extent2D{ 800, 600 });
    }

    TEST_CASE("a surface that defers to the swapchain clamps the preferred size")
    {
        SUBCASE("a preferred size inside the range is used as-is")
        {
            auto extent = chooseSwapchainExtent(
                Extent2D{ 1280, 720 },
                Extent2D{ 640, 480 },
                Extent2D{ 4096, 4096 },
                NoCurrentExtent);

            CHECK(extent == Extent2D{ 1280, 720 });
        }

        SUBCASE("a preferred size below the minimum is raised")
        {
            auto extent = chooseSwapchainExtent(
                Extent2D{ 320, 200 },
                Extent2D{ 640, 480 },
                Extent2D{ 4096, 4096 },
                NoCurrentExtent);

            CHECK(extent == Extent2D{ 640, 480 });
        }

        SUBCASE("a preferred size above the maximum is lowered")
        {
            auto extent = chooseSwapchainExtent(
                Extent2D{ 8192, 8192 },
                Extent2D{ 640, 480 },
                Extent2D{ 4096, 2160 },
                NoCurrentExtent);

            CHECK(extent == Extent2D{ 4096, 2160 });
        }

        SUBCASE("each axis is clamped independently")
        {
            auto extent = chooseSwapchainExtent(
                Extent2D{ 320, 8192 },
                Extent2D{ 640, 480 },
                Extent2D{ 4096, 2160 },
                NoCurrentExtent);

            CHECK(extent == Extent2D{ 640, 2160 });
        }
    }

    TEST_CASE("a half-sentinel current extent is not trusted")
    {
        // Vulkan reports the sentinel on both axes at once, so a single sentinel axis is
        // nonsense -- fall back to clamping rather than propagating uint32_t::max as a size.
        auto extent = chooseSwapchainExtent(
            Extent2D{ 1280, 720 },
            Extent2D{ 640, 480 },
            Extent2D{ 4096, 2160 },
            Extent2D{ std::numeric_limits<std::uint32_t>::max(), 600 });

        CHECK(extent == Extent2D{ 1280, 720 });
    }

    TEST_CASE("the preferred image count is used when the surface allows it")
    {
        CHECK(chooseImageCount(3, 1, 8) == std::uint32_t{ 3 });
    }

    TEST_CASE("a preferred image count below the surface minimum is raised")
    {
        CHECK(chooseImageCount(3, 5, 8) == std::uint32_t{ 5 });
    }

    TEST_CASE("a preferred image count above the surface maximum is lowered")
    {
        CHECK(chooseImageCount(3, 1, 2) == std::uint32_t{ 2 });
    }

    TEST_CASE("a maximum of zero means the surface imposes no upper bound")
    {
        CHECK(chooseImageCount(3, 1, 0) == std::uint32_t{ 3 });

        SUBCASE("the minimum still applies")
        {
            CHECK(chooseImageCount(3, 6, 0) == std::uint32_t{ 6 });
        }
    }
}

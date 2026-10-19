#include <doctest/doctest.h>

import std;
import aegis.rhi;

namespace
{
using namespace aegis::rhi::detail;

constexpr std::uint32_t api13{ minApiVersion };

auto candidate(DeviceKind kind, std::uint32_t maxImageDimension2D = 16384) -> DeviceCandidate
{
    return { .kind = kind,
        .apiVersion = api13,
        .maxImageDimension2D = maxImageDimension2D,
        .queuesComplete = true,
        .extensionsSupported = true };
}
}

TEST_SUITE("rhi::rankPhysicalDevice")
{
    TEST_CASE("an integrated GPU beats a CPU device at equal limits")
    {
        const auto integrated = rankPhysicalDevice(candidate(DeviceKind::Integrated));
        const auto cpu = rankPhysicalDevice(candidate(DeviceKind::Cpu));
        REQUIRE(integrated.has_value());
        REQUIRE(cpu.has_value());
        CHECK(*integrated > *cpu);
    }

    TEST_CASE("the device kind order is discrete, integrated, virtual, CPU, other")
    {
        const auto discrete = *rankPhysicalDevice(candidate(DeviceKind::Discrete));
        const auto integrated = *rankPhysicalDevice(candidate(DeviceKind::Integrated));
        const auto virtualGpu = *rankPhysicalDevice(candidate(DeviceKind::Virtual));
        const auto cpu = *rankPhysicalDevice(candidate(DeviceKind::Cpu));
        const auto other = *rankPhysicalDevice(candidate(DeviceKind::Other));
        CHECK(discrete > integrated);
        CHECK(integrated > virtualGpu);
        CHECK(virtualGpu > cpu);
        CHECK(cpu > other);
    }

    TEST_CASE("a larger image limit does not lift a CPU device above an integrated GPU")
    {
        const auto integrated = *rankPhysicalDevice(candidate(DeviceKind::Integrated, 16384));
        const auto cpu = *rankPhysicalDevice(candidate(DeviceKind::Cpu, 32768));
        CHECK(integrated > cpu);
    }

    TEST_CASE("within one kind the larger image limit wins")
    {
        CHECK(*rankPhysicalDevice(candidate(DeviceKind::Discrete, 32768)) >
              *rankPhysicalDevice(candidate(DeviceKind::Discrete, 16384)));
    }

    TEST_CASE("a device that is unsuitable has no rank")
    {
        auto incompleteQueues = candidate(DeviceKind::Discrete);
        incompleteQueues.queuesComplete = false;
        CHECK_FALSE(rankPhysicalDevice(incompleteQueues).has_value());

        auto missingExtensions = candidate(DeviceKind::Discrete);
        missingExtensions.extensionsSupported = false;
        CHECK_FALSE(rankPhysicalDevice(missingExtensions).has_value());

        auto oldApi = candidate(DeviceKind::Discrete);
        oldApi.apiVersion = (1u << 22) | (2u << 12) | 198u; // 1.2.198
        CHECK_FALSE(rankPhysicalDevice(oldApi).has_value());
    }

    TEST_CASE("a device reporting a newer API than the minimum is suitable")
    {
        auto newer = candidate(DeviceKind::Discrete);
        newer.apiVersion = (1u << 22) | (4u << 12) | 5u;
        CHECK(rankPhysicalDevice(newer).has_value());
    }
}

TEST_SUITE("rhi::matchesDeviceName")
{
    TEST_CASE("an empty filter matches every name")
    {
        CHECK(matchesDeviceName("NVIDIA GeForce RTX 4070", ""));
        CHECK(matchesDeviceName("", ""));
    }

    TEST_CASE("the filter matches a substring regardless of case")
    {
        CHECK(matchesDeviceName("NVIDIA GeForce RTX 4070", "rtx"));
        CHECK(matchesDeviceName("llvmpipe (LLVM 18.1.3, 256 bits)", "LLVMPIPE"));
    }

    TEST_CASE("a filter that is not contained does not match")
    {
        CHECK_FALSE(matchesDeviceName("Intel(R) UHD Graphics", "nvidia"));
        CHECK_FALSE(matchesDeviceName("", "x"));
    }
}

TEST_SUITE("rhi::pickQueueFamilies")
{
    constexpr std::uint32_t none{ noQueueFamily };

    TEST_CASE("a single universal family serves every role")
    {
        const std::array families{ QueueFamilyInfo{ true, true, true, true } };
        const auto selection = pickQueueFamilies(families, true);
        CHECK(selection.graphics == 0);
        CHECK(selection.compute == 0);
        CHECK(selection.transfer == 0);
        CHECK(selection.present == 0);
        CHECK(selection.isComplete(true));
    }

    TEST_CASE("compute and transfer prefer a dedicated family")
    {
        const std::array families{
            QueueFamilyInfo{ true, true, true, true },
            QueueFamilyInfo{ false, true, true, false },
            QueueFamilyInfo{ false, false, true, false },
        };
        const auto selection = pickQueueFamilies(families, true);
        CHECK(selection.graphics == 0);
        CHECK(selection.compute == 1);
        CHECK(selection.transfer == 2);
    }

    TEST_CASE("compute and transfer fall back to graphics without a dedicated family")
    {
        const std::array families{ QueueFamilyInfo{ true, true, true, true } };
        const auto selection = pickQueueFamilies(families, false);
        CHECK(selection.compute == selection.graphics);
        CHECK(selection.transfer == selection.graphics);
        CHECK(selection.isComplete(false));
    }

    TEST_CASE("present prefers the graphics family")
    {
        const std::array families{
            QueueFamilyInfo{ false, false, true, true },
            QueueFamilyInfo{ true, true, true, true },
        };
        const auto selection = pickQueueFamilies(families, true);
        CHECK(selection.graphics == 1);
        CHECK(selection.present == 1);
    }

    TEST_CASE("present uses another family when graphics cannot present")
    {
        const std::array families{
            QueueFamilyInfo{ true, true, true, false },
            QueueFamilyInfo{ false, false, true, true },
        };
        const auto selection = pickQueueFamilies(families, true);
        CHECK(selection.graphics == 0);
        CHECK(selection.present == 1);
    }

    TEST_CASE("present never falls back to graphics")
    {
        const std::array families{ QueueFamilyInfo{ true, true, true, false } };
        const auto selection = pickQueueFamilies(families, true);
        CHECK(selection.graphics == 0);
        CHECK(selection.present == none);
        CHECK_FALSE(selection.isComplete(true));
        CHECK(selection.isComplete(false));
    }

    TEST_CASE("a headless selection has no present family")
    {
        const std::array families{ QueueFamilyInfo{ true, true, true, true } };
        const auto selection = pickQueueFamilies(families, false);
        CHECK(selection.present == none);
    }

    TEST_CASE("without a graphics family nothing is complete")
    {
        const std::array families{ QueueFamilyInfo{ false, true, true, true } };
        const auto selection = pickQueueFamilies(families, true);
        CHECK(selection.graphics == none);
        CHECK_FALSE(selection.isComplete(false));
        CHECK_FALSE(selection.isComplete(true));
    }

    TEST_CASE("no families select nothing")
    {
        const auto selection = pickQueueFamilies({}, true);
        CHECK_FALSE(selection.isComplete(false));
    }
}

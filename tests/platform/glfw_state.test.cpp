#include <doctest/doctest.h>

import std;
import aegis.platform;

using namespace aegis::platform;
using namespace aegis::platform::detail;

TEST_SUITE("platform::GlfwErrorCapture")
{
    TEST_CASE("take returns the recorded description and the code")
    {
        GlfwErrorCapture capture;
        capture.record(0x10008, "X11: The DISPLAY environment variable is missing");
        const auto text = capture.take();
        CHECK(text.contains("X11: The DISPLAY environment variable is missing"));
        CHECK(text.contains("0x10008"));
    }

    TEST_CASE("take clears the text so a stale error is not reported twice")
    {
        GlfwErrorCapture capture;
        capture.record(1, "first");
        CHECK_FALSE(capture.take().empty());
        CHECK(capture.take().empty());
    }

    TEST_CASE("record keeps only the latest error")
    {
        GlfwErrorCapture capture;
        capture.record(1, "first");
        capture.record(2, "second");
        const auto text = capture.take();
        CHECK(text.contains("second"));
        CHECK_FALSE(text.contains("first"));
    }
}

TEST_SUITE("platform::InitRefCount")
{
    TEST_CASE("initializes on the first acquire only")
    {
        InitRefCount refs;
        int inits = 0;
        const auto init = [&] { ++inits; return true; };
        CHECK(refs.acquire(init));
        CHECK(refs.acquire(init));
        CHECK(inits == 1);
        CHECK(refs.count() == 2);
    }

    TEST_CASE("terminates on the last release only")
    {
        InitRefCount refs;
        int terminations = 0;
        (void)refs.acquire([] { return true; });
        (void)refs.acquire([] { return true; });
        refs.release([&] { ++terminations; });
        CHECK(terminations == 0);
        refs.release([&] { ++terminations; });
        CHECK(terminations == 1);
        CHECK(refs.count() == 0);
    }

    TEST_CASE("a failed init adds no user and is retried by the next acquire")
    {
        InitRefCount refs;
        int inits = 0;
        CHECK_FALSE(refs.acquire([&] { ++inits; return false; }));
        CHECK(refs.count() == 0);
        CHECK(refs.acquire([&] { ++inits; return true; }));
        CHECK(inits == 2);
        CHECK(refs.count() == 1);
    }

    TEST_CASE("a release without a user does not terminate")
    {
        InitRefCount refs;
        int terminations = 0;
        refs.release([&] { ++terminations; });
        CHECK(terminations == 0);
        CHECK(refs.count() == 0);
    }
}

TEST_SUITE("platform::initError")
{
    TEST_CASE("a failed init reports GLFW's reason")
    {
        // The seam for 'no display': glfwInit fails and GLFW's error callback captured why.
        GlfwErrorCapture capture;
        capture.record(0x10008, "X11: The DISPLAY environment variable is missing");
        const auto error = initError(capture.take());
        CHECK(error.code == ErrorCode::InitFailed);
        CHECK(error.message.contains("DISPLAY environment variable is missing"));
        CHECK(describe(error).starts_with("InitFailed: Failed to initialize GLFW: "));
    }

    TEST_CASE("an init failure without a reason still says so")
    {
        CHECK(initError("").message.contains("no reason given"));
    }

    TEST_CASE("a failed window creation reports GLFW's reason")
    {
        const auto error = windowError("Vulkan: Loader not found");
        CHECK(error.code == ErrorCode::WindowCreationFailed);
        CHECK(error.message.contains("Vulkan: Loader not found"));
    }
}

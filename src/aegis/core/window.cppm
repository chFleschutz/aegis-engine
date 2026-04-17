module;

#include "core/assert.h"

#include <GLFW/glfw3.h>

#include <string>

export module Aegis.Core.Window;

export namespace Aegis::Core
{
	class Window
	{
	public:
		Window(int width, int height, std::string title)
			: m_width(width), m_height(height), m_windowTitle(std::move(title))
		{
			glfwInit();
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

			m_window = glfwCreateWindow(m_width, m_height, m_windowTitle.c_str(), nullptr, nullptr);
			AGX_ASSERT_X(m_window, "Failed to create GLFW window");

			glfwSetWindowUserPointer(m_window, this);
			glfwSetWindowSizeCallback(m_window, onWindowResize);
		}

		Window(const Window&) = delete;
		Window(Window&&) = delete;
		~Window()
		{
			glfwDestroyWindow(m_window);
			glfwTerminate();
		}

		auto operator=(const Window&) -> Window & = delete;
		auto operator=(Window&&) -> Window & = delete;

		[[nodiscard]] auto glfwWindow() const -> GLFWwindow* { return m_window; }
		[[nodiscard]] auto shouldClose() const -> bool { return glfwWindowShouldClose(m_window); }
		[[nodiscard]] auto extent() const -> std::pair<uint32_t, uint32_t> { return { m_width, m_height }; }
		[[nodiscard]] auto width() const -> uint32_t { return m_width; }
		[[nodiscard]] auto height() const -> uint32_t { return m_height; }
		[[nodiscard]] auto wasResized() const -> bool { return m_windowResized; }

		void resetResizedFlag() { m_windowResized = false; }

	private:
		static void onWindowResize(GLFWwindow* glfwWindow, int newWidth, int newHeight)
		{
			auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
			window->m_windowResized = true;
			window->m_width = static_cast<uint32_t>(newWidth);
			window->m_height = static_cast<uint32_t>(newHeight);
		}

		GLFWwindow* m_window = nullptr;
		std::string m_windowTitle;
		uint32_t m_width;
		uint32_t m_height;
		bool m_windowResized = false;
	};
}

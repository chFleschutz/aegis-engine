module;

#include "core/assert.h"

#include <GLFW/glfw3.h>

#include <unordered_map>

export module Aegis.Core.Input;

import Aegis.Math;
import Aegis.Core.Window;

export namespace Aegis
{
	class Input
	{
	public:
		enum KeyEvent
		{
			Press = GLFW_PRESS,
			Release = GLFW_RELEASE,
			Repeat = GLFW_REPEAT
		};

		/// @note Can be combined with bitwise OR
		enum Modifier
		{
			None = 0,
			Shift = GLFW_MOD_SHIFT,
			Control = GLFW_MOD_CONTROL,
			Alt = GLFW_MOD_ALT,
			Super = GLFW_MOD_SUPER,
			CapsLock = GLFW_MOD_CAPS_LOCK,
			NumLock = GLFW_MOD_NUM_LOCK
		};

		enum MouseButton
		{
			Mouse1 = GLFW_MOUSE_BUTTON_1,
			Mouse2 = GLFW_MOUSE_BUTTON_2,
			Mouse3 = GLFW_MOUSE_BUTTON_3,
			Mouse4 = GLFW_MOUSE_BUTTON_4,
			Mouse5 = GLFW_MOUSE_BUTTON_5,
			Mouse6 = GLFW_MOUSE_BUTTON_6,
			Mouse7 = GLFW_MOUSE_BUTTON_7,
			Mouse8 = GLFW_MOUSE_BUTTON_8,
			MouseLast = GLFW_MOUSE_BUTTON_LAST,
			MouseLeft = GLFW_MOUSE_BUTTON_LEFT,
			MouseRight = GLFW_MOUSE_BUTTON_RIGHT,
			MouseMiddle = GLFW_MOUSE_BUTTON_MIDDLE
		};

		enum Key
		{
			Unknown = GLFW_KEY_UNKNOWN,

			Space = GLFW_KEY_SPACE,
			Apostrophe = GLFW_KEY_APOSTROPHE,
			Comma = GLFW_KEY_COMMA,
			Minus = GLFW_KEY_MINUS,
			Period = GLFW_KEY_PERIOD,
			Slash = GLFW_KEY_SLASH,
			Zero = GLFW_KEY_0,
			One = GLFW_KEY_1,
			Two = GLFW_KEY_2,
			Three = GLFW_KEY_3,
			Four = GLFW_KEY_4,
			Five = GLFW_KEY_5,
			Six = GLFW_KEY_6,
			Seven = GLFW_KEY_7,
			Eight = GLFW_KEY_8,
			Nine = GLFW_KEY_9,
			Semicolon = GLFW_KEY_SEMICOLON,
			Equal = GLFW_KEY_EQUAL,

			A = GLFW_KEY_A,
			B = GLFW_KEY_B,
			C = GLFW_KEY_C,
			D = GLFW_KEY_D,
			E = GLFW_KEY_E,
			F = GLFW_KEY_F,
			G = GLFW_KEY_G,
			H = GLFW_KEY_H,
			I = GLFW_KEY_I,
			J = GLFW_KEY_J,
			K = GLFW_KEY_K,
			L = GLFW_KEY_L,
			M = GLFW_KEY_M,
			N = GLFW_KEY_N,
			O = GLFW_KEY_O,
			P = GLFW_KEY_P,
			Q = GLFW_KEY_Q,
			R = GLFW_KEY_R,
			S = GLFW_KEY_S,
			T = GLFW_KEY_T,
			U = GLFW_KEY_U,
			V = GLFW_KEY_V,
			W = GLFW_KEY_W,
			X = GLFW_KEY_X,
			Y = GLFW_KEY_Y,
			Z = GLFW_KEY_Z,

			LeftBracket = GLFW_KEY_LEFT_BRACKET,
			Backslash = GLFW_KEY_BACKSLASH,
			RightBracket = GLFW_KEY_RIGHT_BRACKET,
			GraveAccent = GLFW_KEY_GRAVE_ACCENT,
			World1 = GLFW_KEY_WORLD_1,
			World2 = GLFW_KEY_WORLD_2,
			Escape = GLFW_KEY_ESCAPE,
			Enter = GLFW_KEY_ENTER,
			Tab = GLFW_KEY_TAB,
			Backspace = GLFW_KEY_BACKSPACE,
			Insert = GLFW_KEY_INSERT,
			Delete = GLFW_KEY_DELETE,
			Right = GLFW_KEY_RIGHT,
			Left = GLFW_KEY_LEFT,
			Down = GLFW_KEY_DOWN,
			Up = GLFW_KEY_UP,
			PageUp = GLFW_KEY_PAGE_UP,
			PageDown = GLFW_KEY_PAGE_DOWN,
			Home = GLFW_KEY_HOME,
			End = GLFW_KEY_END,
			CapsLockKey = GLFW_KEY_CAPS_LOCK,    // CapsLock is also valid
			ScrollLock = GLFW_KEY_SCROLL_LOCK,
			NumLockKey = GLFW_KEY_NUM_LOCK,      // NumLock is also valid
			PrintScreen = GLFW_KEY_PRINT_SCREEN,
			Pause = GLFW_KEY_PAUSE,

			F1 = GLFW_KEY_F1,
			F2 = GLFW_KEY_F2,
			F3 = GLFW_KEY_F3,
			F4 = GLFW_KEY_F4,
			F5 = GLFW_KEY_F5,
			F6 = GLFW_KEY_F6,
			F7 = GLFW_KEY_F7,
			F8 = GLFW_KEY_F8,
			F9 = GLFW_KEY_F9,
			F10 = GLFW_KEY_F10,
			F11 = GLFW_KEY_F11,
			F12 = GLFW_KEY_F12,
			F13 = GLFW_KEY_F13,
			F14 = GLFW_KEY_F14,
			F15 = GLFW_KEY_F15,
			F16 = GLFW_KEY_F16,
			F17 = GLFW_KEY_F17,
			F18 = GLFW_KEY_F18,
			F19 = GLFW_KEY_F19,
			F20 = GLFW_KEY_F20,
			F21 = GLFW_KEY_F21,
			F22 = GLFW_KEY_F22,
			F23 = GLFW_KEY_F23,
			F24 = GLFW_KEY_F24,
			F25 = GLFW_KEY_F25,

			KeyPad0 = GLFW_KEY_KP_0,
			KeyPad1 = GLFW_KEY_KP_1,
			KeyPad2 = GLFW_KEY_KP_2,
			KeyPad3 = GLFW_KEY_KP_3,
			KeyPad4 = GLFW_KEY_KP_4,
			KeyPad5 = GLFW_KEY_KP_5,
			KeyPad6 = GLFW_KEY_KP_6,
			KeyPad7 = GLFW_KEY_KP_7,
			KeyPad8 = GLFW_KEY_KP_8,
			KeyPad9 = GLFW_KEY_KP_9,
			KeyPadDecimal = GLFW_KEY_KP_DECIMAL,
			KeyPadDivide = GLFW_KEY_KP_DIVIDE,
			KeyPadMultiply = GLFW_KEY_KP_MULTIPLY,
			KeyPadSubtract = GLFW_KEY_KP_SUBTRACT,
			KeyPadAdd = GLFW_KEY_KP_ADD,
			KeyPadEnter = GLFW_KEY_KP_ENTER,
			KeyPadEqual = GLFW_KEY_KP_EQUAL,

			LeftShift = GLFW_KEY_LEFT_SHIFT,
			LeftControl = GLFW_KEY_LEFT_CONTROL,
			LeftAlt = GLFW_KEY_LEFT_ALT,
			LeftSuper = GLFW_KEY_LEFT_SUPER,
			RightShift = GLFW_KEY_RIGHT_SHIFT,
			RightControl = GLFW_KEY_RIGHT_CONTROL,
			RightAlt = GLFW_KEY_RIGHT_ALT,
			RightSuper = GLFW_KEY_RIGHT_SUPER,

			Menu = GLFW_KEY_MENU,
			MaxValue = GLFW_KEY_LAST
		};

		Input(const Core::Window& window)
			: m_window{ window.glfwWindow() }
		{
			AGX_ASSERT_X(s_instance == nullptr, "Cannot create Input: Only one instance of Input is allowed");
			s_instance = this;

			if (glfwRawMouseMotionSupported())
				glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

			// Set up key callback
			glfwSetKeyCallback(m_window, [](GLFWwindow*, int key, int scancode, int action, int mods)
				{
					Input::instance().glfwKeyCallback(key, scancode, static_cast<KeyEvent>(action), static_cast<Modifier>(mods));
				});
		}

		Input(const Input&) = delete;
		Input(Input&&) = delete;
		~Input()
		{
			s_instance = nullptr;
		}

		Input& operator=(const Input&) = delete;
		Input& operator=(Input&&) = delete;

		/// @brief Acces to the instance of Input
		/// @return Returns a reference to the instance of Input
		/// @note Make sure Input was initialized 
		static Input& instance()
		{
			AGX_ASSERT_X(s_instance != nullptr, "Input instance is not created");
			return *s_instance;
		}

		/// @brief Returns the state of key
		/// @param key Keycode of the key
		/// @return Returns true if the key is pressed otherwise false
		bool keyPressed(Key key)
		{
			return glfwGetKey(m_window, key) == GLFW_PRESS;
		}

		/// @brief Returns the state of button
		/// @param button Buttoncode of the mousebutton
		/// @return Returns true if the button is pressed otherwise false
		bool mouseButtonPressed(MouseButton button)
		{
			return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
		}

		/// @brief Retrieves the mouse cursor position
		/// @return Returns a vec2 with the cursor position
		glm::vec2 cursorPosition()
		{
			double xPos, yPos;
			glfwGetCursorPos(m_window, &xPos, &yPos);
			return { xPos, yPos };
		}

		/// @brief Sets the input mode
		/// @param mode The mode to change
		/// @param value The new value for the mode
		void setInputMode(int mode, int value)
		{
			glfwSetInputMode(m_window, mode, value);
		}

		/// @brief Binds a function to a key
		/// @tparam T Type of the class that contains the function
		/// @param instance Pointer to the instance of the class
		/// @param func Function to bind
		/// @param key to bind the function to (A, B, C, etc)
		/// @param event which triggers the function (press, release, etc)
		/// @param mod Any modifier keys that should be pressed (shift, control, etc)
		/// @note The function will be executed in glfwPollEvents
		template<typename T>
		void bind(T* instance, void (T::* func)(), Key key, KeyEvent event = Press, Modifier mod = None)
		{
			m_keyBindings[key].push_back({ std::bind(func, instance), event, mod });
		}

	private:
		struct Binding
		{
			std::function<void()> function;
			KeyEvent event;
			Modifier mods;
		};

		/// @brief Calls the bound functions for the key
		void glfwKeyCallback(int key, int scancode, KeyEvent action, Modifier mods)
		{
			for (const auto& binding : m_keyBindings[key])
			{
				if (binding.event == action and binding.mods == mods)
					binding.function();
			}
		}

		static Input* s_instance;

		GLFWwindow* m_window = nullptr;
		std::unordered_map<int, std::vector<Binding>> m_keyBindings;
	};
}

module;

#include "core/assert.h"

#include <string>
#include <sstream>

export module Aegis.Utils.Color;

import Aegis.Math;

namespace Aegis::Utils
{
	class Color
	{
	public:
		static glm::vec4 red() { return glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f }; }
		static glm::vec4 green() { return glm::vec4{ 0.0f, 1.0f, 0.0f, 1.0f }; }
		static glm::vec4 blue() { return glm::vec4{ 0.0f, 0.0f, 1.0f, 1.0f }; }
		static glm::vec4 white() { return glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f }; }
		static glm::vec4 gray() { return glm::vec4{ 0.5f, 0.5f, 0.5f, 1.0f }; }
		static glm::vec4 black() { return glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f }; }
		static glm::vec4 yellow() { return glm::vec4{ 1.0f, 1.0f, 0.0f, 1.0f }; }
		static glm::vec4 orange() { return glm::vec4{ 1.0f, 0.5f, 0.0f, 1.0f }; }
		static glm::vec4 purple() { return glm::vec4{ 0.5f, 0.0f, 1.0f, 1.0f }; }
		static glm::vec4 pink() { return glm::vec4{ 1.0f, 0.0f, 1.0f, 1.0f }; }
		static glm::vec4 brown() { return glm::vec4{ 0.5f, 0.25f, 0.0f, 1.0f }; }
		static glm::vec4 random()
		{
			return glm::vec4{ Math::Random::uniformFloat(), Math::Random::uniformFloat(), Math::Random::uniformFloat(), 1.0f };
		}

		static glm::vec4 fromHex(const std::string& hex)
		{
			AGX_ASSERT_X(hex.length() == 7 && hex[0] == '#', "Invalid hex color format");

			if (hex.size() != 7 or hex[0] != '#')
				return glm::vec4{ 1.0f };

			unsigned int rgbValue;
			std::istringstream stream(hex.substr(1));
			stream >> std::hex >> rgbValue;

			int red = (rgbValue >> 16) & 0xFF;
			int green = (rgbValue >> 8) & 0xFF;
			int blue = rgbValue & 0xFF;

			return glm::vec4{ red / 255.0f, green / 255.0f, blue / 255.0f, 1.0f };
		}
	};
}

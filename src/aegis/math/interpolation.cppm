module;

#include <cmath>

export module Aegis.Math:Interpolation;

export namespace Aegis::Math
{
	template <typename T>
	T lerp(const T& a, const T& b, float t)
	{
		return a + t * (b - a);
	}


	/// @brief Returns the percentage of a value between a min and max value
	auto percentage(float value, float min, float max) -> float
	{
		return (value - min) / (max - min);
	}

	/// @brief Returns the sigmoid function of x in the range [0, 1]
	auto sigmoid01(float x) -> float
	{
		return 1.0f / (1.0f + std::exp(-12.0f * x + 6.0f));
	}

	/// @brief Returns the (fast approximation) sigmoid function of x in the range [0, 1]
	auto fastSigmoid01(float x) -> float
	{
		x = 12.0f * x - 6.0f;
		return x / (1.0f + std::abs(x)) / 2.0f + 0.5f;
	}

	/// @brief Returns the tangens hyperbolicus of x in the range [0, 1]
	auto tanh01(float x) -> float
	{
		return std::tanh(6.0f * x - 3.0f) / 2.0f + 0.5f;
	}
}
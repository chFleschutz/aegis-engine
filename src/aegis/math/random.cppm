module;

#include <random>

export module Aegis.Math:Random;

export namespace Aegis::Math
{
	class Random
	{
	public:
		/// @brief Seeds the random number generator
		static void seed(unsigned int seed) { s_generator.seed(seed); }

		/// @brief Returns the random number generator
		[[nodiscard]] auto static generator() -> std::mt19937& { return s_generator; }


		/// @brief Returns a uniform random float in the range [min, max]
		[[nodiscard]] static auto uniformFloat(float min = 0.0f, float max = 1.0f) -> float
		{
			std::uniform_real_distribution distribution(min, max);
			return distribution(s_generator);
		}

		/// @brief Returns a normal random float with the given mean and standard deviation
		[[nodiscard]] static auto normalFloat(float mean, float stddev) -> float
		{
			std::normal_distribution<float> distribution(mean, stddev);
			return distribution(s_generator);
		}

		/// @brief Returns a normal random float in the range [min, max]
		[[nodiscard]] static auto normalFloatRange(float min = 0.0f, float max = 1.0f) -> float
		{
			return std::clamp(normalFloat((min + max) / 2.0f, (max - min) / 6.0f), min, max);
		}


		/// @brief Returns a uniform random int in the range [min, max]
		[[nodiscard]] static auto uniformInt(int min, int max) -> int
		{
			std::uniform_int_distribution distribution(min, max);
			return distribution(s_generator);
		}

		/// @brief Returns a normal random int with the given mean and standard deviation
		[[nodiscard]] static auto normalInt(float mean, float stddev) -> int
		{
			std::normal_distribution<float> distribution(mean, stddev);
			return std::lround(distribution(s_generator));
		}

		/// @brief Returns a normal random int in the range [min, max]	
		[[nodiscard]] static auto normalIntRange(int min, int max) -> int
		{
			return std::clamp(normalInt((min + max) / 2.0f, (max - min) / 6.0f), min, max);
		}

	private:
		inline static std::mt19937 s_generator{ std::random_device{}() };
	};
}

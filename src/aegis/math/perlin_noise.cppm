module;

#include "core/assert.h"

#include <cmath>
#include <vector>

export module Aegis.Math:PerlinNoise;

import :Random;
import :Interpolation;

namespace Aegis::Math
{
	/// @brief 1D Perlin noise generator.
	class PerlinNoise1D
	{
	public:
		/// @brief Creates a new Perlin noise random number generator.
		/// @note The noise is generated using the Random class, for the same result seed it BEFORE creating PerlinNoise1D object.
		explicit PerlinNoise1D(float intervalSize = 1.0f)
			: m_bandwidth(intervalSize)
		{
			m_intervals.emplace_back(Interval(Random::uniformFloat()));
		}

		/// @brief Returns the noise value at the given position.
		float noise(float x, int rank, float persistence = 0.5f)
		{
			AGX_ASSERT_X(x >= 0.0f, "Perlin noise only works for positive values");

			int xIntervalIndex = static_cast<int>(x / m_bandwidth); // Get the interval of x
			float xRelative = x / m_bandwidth - xIntervalIndex;		// Transform x to the interval [0, 1)

			// Create new intervals if needed
			while (m_intervals.size() - 1 < xIntervalIndex)
			{
				addInterval();
			}

			// Create new octaves if needed
			auto& xInterval = m_intervals[xIntervalIndex];
			while (xInterval.octaves.size() <= rank)
			{
				addOctave(xIntervalIndex);
			}

			// Calculate noise value for all octaves until rank
			float noiseValue = 0.0f;
			float maxAmplitude = 0.0f;
			for (const auto& octave : xInterval.octaves)
			{
				if (octave.rank > rank)
					break;

				float amplitude = static_cast<float>(std::pow(persistence, octave.rank));
				noiseValue += octave.value(xRelative) * amplitude;
				maxAmplitude += amplitude;
			}

			// Return normalized noise value
			return noiseValue / maxAmplitude;
		}

	private:
		/// @brief Octave holds the signal values of a single octave with more values (higher LOD) per rank.
		struct Octave
		{
			Octave(int rank, float firstValue, float lastValue)
				: rank(rank)
			{
				signalValues.emplace_back(firstValue);
				signalValues.emplace_back(lastValue);
				// Add all random signal values for the rank
				for (int i = 1; i < rank; i++)
				{
					for (int i = 1; i < signalValues.size(); i += 2)
					{
						signalValues.insert(signalValues.begin() + i, Random::uniformFloat());
					}
				}
			}

			/// @brief Returns the value of the octave at the given position.
			/// @note Relative position x MUST be between [0, 1).
			float value(float x) const
			{
				AGX_ASSERT_X(x >= 0.0f && x < 1.0f, "x must be in the interval [0, 1)");

				int leftIndex = static_cast<int>((signalValues.size() - 1) * x);
				int rightIndex = leftIndex + 1;

				// Calculate the percentage where x is between the two signal values
				float chunkSize = 1.0f / (signalValues.size() - 1);
				float percent = Math::percentage(x, leftIndex * chunkSize, rightIndex * chunkSize);

				return std::lerp(signalValues[leftIndex], signalValues[rightIndex], Math::tanh01(percent));
			}

			int rank;
			std::vector<float> signalValues;
		};

		/// @brief Represents an interval of the noise with multiple octaves.
		/// @note The length of the interval is the bandwidth of the noise but each octave uses relative values between [0, 1).
		struct Interval
		{
			Interval(float firstValue)
			{
				octaves.emplace_back(Octave(1, firstValue, Random::uniformFloat()));
			}

			void addOctave(float firstValue, float lastValue)
			{
				octaves.emplace_back(Octave(octaves.back().rank + 1, firstValue, lastValue));
			}

			std::vector<Octave> octaves;
		};

		void addInterval()
		{
			// Add new interval with the last value of the previous interval as its first value
			const auto& previousFirstOctave = m_intervals.back().octaves.front();
			m_intervals.emplace_back(Interval(previousFirstOctave.signalValues.back()));
		}

		void addOctave(int intervalIndex)
		{
			AGX_ASSERT_X(intervalIndex >= 0 && intervalIndex < m_intervals.size(), "Can't add octave, because of invalid interval index");

			float firstValue = 0.0f;
			float lastValue = 0.0f;
			int rank = m_intervals[intervalIndex].octaves.back().rank + 1;

			// Get the last value of the previous interval if possible
			if (intervalIndex - 1 >= 0 and m_intervals[intervalIndex - 1].octaves.size() >= rank)
			{
				const auto& octave = m_intervals[intervalIndex - 1].octaves[rank - 1];
				firstValue = octave.signalValues.back();
			}
			else
			{
				firstValue = Random::uniformFloat();
			}

			// Get the first value of the next interval if possible
			if (intervalIndex + 1 < m_intervals.size() and m_intervals[intervalIndex + 1].octaves.size() >= rank)
			{
				const auto& octavei = m_intervals[intervalIndex + 1].octaves[rank - 1];
				lastValue = octavei.signalValues.front();
			}
			else
			{
				lastValue = Random::uniformFloat();
			}

			// Add the octave
			m_intervals[intervalIndex].addOctave(firstValue, lastValue);
		}

		float m_bandwidth;
		std::vector<Interval> m_intervals;
	};
}

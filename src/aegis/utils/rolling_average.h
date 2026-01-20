#pragma once

namespace Aegis
{
	/// @brief Rolling average over N values
	/// @tparam N Number of values to average (must be greater than zero)
	template<size_t N>
		requires (N > 0)
	class RollingAverage
	{
	public:
		RollingAverage() = default;
		RollingAverage(const RollingAverage&) = default;
		RollingAverage(RollingAverage&&) = default;
		~RollingAverage() = default;

		RollingAverage& operator=(const RollingAverage&) = default;
		RollingAverage& operator=(RollingAverage&&) = default;

		operator double() const { return average(); }
		void operator+=(double value) { add(value); }

		/// @brief Add a new value to the rolling average
		void add(double value)
		{
			m_sum -= m_values[m_index];
			m_values[m_index] = value;
			m_sum += value;
			m_index = (m_index + 1) % N;
		}

		/// @brief Get the average of the N values
		/// @note Average is dirty for the first N-1 values
		auto average() const -> double
		{
			return m_sum / N;
		}

		auto last() const -> double
		{
			size_t lastIndex = (m_index + N - 1) % N;
			return m_values[lastIndex];
		}

	private:
		std::array<double, N> m_values{};
		double m_sum = 0.0;
		size_t m_index = 0;
	};
}

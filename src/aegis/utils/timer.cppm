module;

#include <chrono>
#include <iostream>
#include <string_view>

export module Aegis.Utils.Timer;

export namespace Aegis::Utils
{
	class Timer
	{
	public:
		Timer() : m_start{ std::chrono::high_resolution_clock::now() } {}
		~Timer() = default;

		void reStart() { m_start = std::chrono::high_resolution_clock::now(); }

		template <typename Unit>
		auto elapsed() const -> Unit
		{
			const auto end = std::chrono::high_resolution_clock::now();
			return std::chrono::duration_cast<Unit>(end - m_start);
		}

		template <typename T, typename Unit>
		auto elapsed() const -> T
		{
			const auto end = std::chrono::high_resolution_clock::now();
			return std::chrono::duration<T, Unit>(end - m_start).count();
		}

		auto elapsedNanos() const -> double { return elapsed<double, std::nano>(); }
		auto elapsedMicros() const -> double { return elapsed<double, std::micro>(); }
		auto elapsedMillis() const -> double { return elapsed<double, std::milli>(); }
		auto elapsedSeconds() const -> double { return elapsed<double, std::ratio<1>>(); }

	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
	};


	template <typename Unit = std::chrono::microseconds>
	class ScopeTimer
	{
	public:
		ScopeTimer(std::string_view name = "Duration", std::ostream& os = std::cout)
			: m_name{ name }, m_os{ os }
		{}
		~ScopeTimer() { m_os << std::format("{}: {:>8}\n", m_name, m_timer.elapsed<Unit>()); }

	private:
		Timer m_timer;
		std::string m_name;
		std::ostream& m_os;
	};
}

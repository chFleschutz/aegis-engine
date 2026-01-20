#pragma once

#include "utils/rolling_average.h"
#include "utils/timer.h"

#define AGX_PROFILE_SCOPE(name) Aegis::ScopeProfiler profiler##__LINE__(name)
#define AGX_PROFILE_FUNCTION() AGX_PROFILE_SCOPE(__FUNCTION__)

namespace Aegis
{
	/// @brief Utility class for profiling code execution
	/// @note Uses a rolling average over 'AVERAGE_FRAME_COUNT' frames
	class Profiler
	{
	public:
		static constexpr int AVERAGE_FRAME_COUNT = 50;

		Profiler(const Profiler&) = delete;
		Profiler(Profiler&&) = delete;
		~Profiler() = default;

		auto operator=(const Profiler&) -> Profiler& = delete;
		auto operator=(Profiler&&) -> Profiler& = delete;

		[[nodiscard]] static auto instance() -> Profiler&
		{
			static Profiler instance;
			return instance;
		}

		/// @brief Retrieve the average time for a given name or 0.0 if not found
		[[nodiscard]] auto time(const std::string& name) const -> double
		{
			auto it = m_times.find(name);
			if (it == m_times.end())
				return 0.0;
			return it->second.average();
		}

		/// @brief Retrieve the last recorded time
		[[nodiscard]] auto lastTime(const std::string& name) const -> double
		{
			auto it = m_times.find(name);
			if (it == m_times.end())
				return 0.0;
			return it->second.last();
		}

		[[nodiscard]] auto times() const -> const std::unordered_map<std::string, RollingAverage<AVERAGE_FRAME_COUNT>>&
		{
			return m_times;
		}

		void addTime(const std::string& name, double time)
		{
			m_times[name].add(time);
		}

	private:
		Profiler() = default;

		std::unordered_map<std::string, RollingAverage<AVERAGE_FRAME_COUNT>> m_times;
	};


	/// @brief Times the current scope and adds the time to the profiler
	class ScopeProfiler
	{
	public:
		ScopeProfiler(std::string_view name)
			: m_name(name)
		{
		}

		~ScopeProfiler()
		{
			Profiler::instance().addTime(m_name, m_timer.elapsedMillis());
		}

	private:
		Timer m_timer;
		std::string m_name;
	};
}

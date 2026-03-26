module;

#ifdef NDEBUG
#define AEGIS_LOG_DISABLE_DEBUG
#define AEGIS_LOG_DISABLE_TRACE
#endif // NDEBUG

#define AEGIS_LOG_FATAL_NO_ASYNC
#include <aegis-log/log.h>
#include <aegis-log/sinks/console_sink.h>

export module Aegis.Core.Logging;

export namespace Aegis
{
	class Logging
	{
	public:
		Logging()
		{
			ALOG::init<ALOG::DEFAULT_LOGGER, ALOG::NO_THREAD>(ALOG::Severity::Trace)
				.addSink<ALOG::ConsoleSink>();
		}

		static void logo()
		{
			ALOG::info("\n\n\n"
				"\t\t\t\t      ###   #######   ######   ####   ###### \n"
				"\t\t\t\t     ## ##  ##       ##    ##   ##   ##    ##\n"
				"\t\t\t\t    ##  ##  ##       ##         ##   ##      \n"
				"\t\t\t\t   ##   ##  ######   ##         ##    ###### \n"
				"\t\t\t\t  ########  ##       ##  ####   ##         ##\n"
				"\t\t\t\t ##     ##  ##       ##    ##   ##   ##    ##\n"
				"\t\t\t\t##      ##  #######   ######   ####   ###### \n"
				"\n\n");

		}
	};
}
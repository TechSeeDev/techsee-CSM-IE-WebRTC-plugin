#pragma once

#include <string>
#include <fstream>
#include <time.h>

#include <atlsafe.h>
#include <atlstr.h>
#include <atlsimpstr.h>
using namespace ATL;


#include "rtc_base/logging.h"

class LogSinkImpl : public rtc::LogSink
{
	public:
		LogSinkImpl(std::string path, rtc::LoggingSeverity sev);
		~LogSinkImpl();

		/*inline*/ void write(std::string msg);

		static rtc::LoggingSeverity severity;

	private:
		void OnLogMessage(const std::string& message) override;

		inline int gettimeofday(struct timeval* tp);
		inline std::string getCurrentDateTime(std::string s);

	private:
		std::string logPath;
		std::string filePath;

		std::ofstream* lpfs;
		
};


extern LogSinkImpl* g_logSink;

#define RTC_LOG2(sev) RTC_LOG_FILE_LINE(sev, __FILE__, __LINE__)
#define RTC_LOG_F2(sev) RTC_LOG2(sev) << __FUNCTION__ << ": "

#define RTC_LOG_SEV()		RTC_LOG_F2(LogSinkImpl::severity)

#define FUNC_BEGIN()	RTC_LOG_SEV() << "BEGIN"

#define FUNC_END()		RTC_LOG_SEV() << "END"

#define FUNC_END_RET_S(r)	{ \
				RTC_LOG_SEV() << "END - Result: " << #r << " - 0x" << r; \
				return r; \
			}

#define FUNC_END_RET_S2(r)	{ \
				RTC_LOG_SEV() << "END - Result: " << #r << " - 0x" << r; \
			}

#define RTC_LOG_DEBUG(s) { \
							if (g_logSink != nullptr) \
								g_logSink->write(s); \
						}
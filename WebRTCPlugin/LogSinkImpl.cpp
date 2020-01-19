#include "stdafx.h"
#include "LogSinkImpl.h"
#include <chrono>

LogSinkImpl* g_logSink = nullptr;

rtc::LoggingSeverity LogSinkImpl::severity = rtc::LoggingSeverity::LS_VERBOSE;

LogSinkImpl::LogSinkImpl(std::string path, rtc::LoggingSeverity sev)
{
	severity = sev;
	logPath = path;

	std::string timestamp = getCurrentDateTime("timestamp");

	CStringA stimestamp = timestamp.c_str();
	stimestamp.Replace(":", "-");

	filePath = logPath + "\\log_" + stimestamp.GetString() + ".txt";

	lpfs = new std::ofstream(filePath.c_str(), std::ios_base::out | std::ios_base::app);
	*lpfs << "Log started @ " << timestamp << '\n';
}

LogSinkImpl::~LogSinkImpl()
{
	if (lpfs != nullptr)
		lpfs->close();
	lpfs = nullptr;
}

void LogSinkImpl::OnLogMessage(const std::string& message) /*override*/
{
	write(message);
}

inline int LogSinkImpl::gettimeofday(struct timeval* tp)
{
	namespace sc = std::chrono;
	sc::system_clock::duration d = sc::system_clock::now().time_since_epoch();
	sc::seconds s = sc::duration_cast<sc::seconds>(d);
	tp->tv_sec = s.count();
	tp->tv_usec = sc::duration_cast<sc::microseconds>(d - s).count();

	return 0;
}

inline std::string LogSinkImpl::getCurrentDateTime(std::string s)
{
	int millisec;
	struct timeval tv;
	struct tm  tstruct;
	char buf[80] = { 0 }, usec_buf[6] = { 0 };

	gettimeofday(&tv);

	millisec = lrint(tv.tv_usec / 1000.0);
	if (millisec >= 1000)
	{
		millisec -= 1000;
		tv.tv_sec++;
	}

	time_t now = time(0);
	tstruct = *localtime(&now);
	if (s == "timestamp")
		strftime(buf, sizeof(buf), "%Y-%m-%d_%X", &tstruct);
	else if (s == "now")
		strftime(buf, sizeof(buf), "%X", &tstruct);
	else if (s == "date")
		strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);

	if (s == "timestamp" || s == "now")
	{
		sprintf_s(usec_buf, ".%03d", millisec);
		strcat(buf, usec_buf);
	}

	return std::string(buf);
}

/*inline*/ void LogSinkImpl::write(std::string msg)
{
	if (!lpfs || (lpfs && !lpfs->is_open()))
		return;

	std::string now = getCurrentDateTime("now");
	std::string s = "[" + now + "] " + msg;
	*lpfs << s;
	OutputDebugStringA(s.c_str());
}
#include "webos/WebOSLog.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>

#include <unistd.h>

#if defined(__GLIBC__) && defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define MCBETA_HAVE_BACKTRACE 1
#endif
#endif

namespace webos
{

void log(const char *format, ...)
{
	static std::mutex mutex;
	std::lock_guard<std::mutex> lock(mutex);

	static std::FILE *file = nullptr;
	static bool opened = false;
	if (!opened)
	{
		opened = true;
		const char *path = std::getenv("MCBETA_WEBOS_LOG");
		if (path == nullptr || *path == '\0')
			path = "/tmp/mcbetacpp-webos.log";
		file = std::fopen(path, "w");
	}

	char buffer[1024];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	std::fprintf(stderr, "%s\n", buffer);
	if (file != nullptr)
	{
		std::fprintf(file, "%s\n", buffer);
		std::fflush(file);
	}
}


}

namespace
{
std::atomic<const char *> g_stage{"(startup)"};
std::atomic<unsigned long> g_stageSerial{0};
std::atomic<unsigned long> g_frames{0};

// Async-signal-safe-ish direct write to stderr (no mutex, no allocation).
void rawWrite(const char *text)
{
	if (text == nullptr)
		return;
	std::size_t n = std::strlen(text);
	ssize_t r = ::write(STDERR_FILENO, text, n);
	(void)r;
}

void crashSignalHandler(int sig)
{
	char line[192];
	std::snprintf(line, sizeof(line), "\n[CRASH] fatal signal %d in stage '%s' (frames rendered: %lu)\n",
	              sig, g_stage.load(), g_frames.load());
	rawWrite(line);
#ifdef MCBETA_HAVE_BACKTRACE
	void *frames[48];
	int count = ::backtrace(frames, 48);
	::backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
	// Also leave a trace in the log file for people without a terminal.
	const char *path = std::getenv("MCBETA_WEBOS_LOG");
	if (path == nullptr || *path == '\0')
		path = "/tmp/mcbetacpp-webos.log";
	if (std::FILE *f = std::fopen(path, "a"))
	{
		std::fputs(line, f);
		std::fclose(f);
	}
	std::signal(sig, SIG_DFL);
	std::raise(sig);
}

void terminateHandler()
{
	rawWrite("\n[CRASH] std::terminate called (uncaught exception) in stage '");
	rawWrite(g_stage.load());
	rawWrite("'\n");
	try
	{
		std::exception_ptr e = std::current_exception();
		if (e)
			std::rethrow_exception(e);
	}
	catch (const std::exception &ex)
	{
		rawWrite("[CRASH] what(): ");
		rawWrite(ex.what());
		rawWrite("\n");
	}
	catch (...)
	{
		rawWrite("[CRASH] (non-standard exception)\n");
	}
	std::abort();
}

void watchdogMain()
{
	unsigned long lastSerial = ~0ul;
	unsigned long lastFrames = ~0ul;
	int secondsSame = 0;
	int seconds = 0;
	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		seconds++;
		const unsigned long serial = g_stageSerial.load();
		const unsigned long frames = g_frames.load();
		if (serial == lastSerial && frames == lastFrames)
			secondsSame++;
		else
			secondsSame = 0;
		lastSerial = serial;
		lastFrames = frames;

		// Report a stall after 3 s without progress, then every 5 s; and a
		// short "alive" line for the first minute so a running-but-black
		// screen is distinguishable from a hang.
		const bool stalled = secondsSame >= 3 && (secondsSame - 3) % 5 == 0;
		const bool alive = secondsSame == 0 && seconds <= 60 && seconds % 5 == 0;
		if (stalled || alive)
		{
			char line[224];
			std::snprintf(line, sizeof(line), "[WATCHDOG] t=%ds stage='%s' frames=%lu %s\n", seconds,
			              g_stage.load(), frames, stalled ? "NO PROGRESS (main thread stuck here)" : "running");
			rawWrite(line);
		}
	}
}
}

namespace webos
{

void stage(const char *name)
{
	g_stage.store(name);
	g_stageSerial.fetch_add(1);
}

void frame()
{
	g_frames.fetch_add(1);
}

void installDiagnostics()
{
	static bool installed = false;
	if (installed)
		return;
	installed = true;
	std::signal(SIGSEGV, crashSignalHandler);
	std::signal(SIGABRT, crashSignalHandler);
	std::signal(SIGBUS, crashSignalHandler);
	std::signal(SIGFPE, crashSignalHandler);
	std::signal(SIGILL, crashSignalHandler);
	std::set_terminate(terminateHandler);
	std::thread(watchdogMain).detach();
}

}

#include "webos/WebOSLog.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
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

// ---------------------------------------------------------------------------
// Environment defaults
//
// Tesselator::instance (a global) creates the SDL window and GL context during
// static initialisation, i.e. BEFORE main() runs, so anything the window needs
// has to be set up here, from a constructor with a priority that runs before
// ordinary C++ globals. Started from an SSH shell the launcher's environment is
// missing: SDL then fails with "XDG_RUNTIME_DIR not set" / "No available video
// device".
// ---------------------------------------------------------------------------
namespace
{
__attribute__((constructor(101))) void webosEnvironmentDefaults()
{
	if (std::getenv("XDG_RUNTIME_DIR") == nullptr || *std::getenv("XDG_RUNTIME_DIR") == '\0')
	{
		// The TV's Wayland socket lives here.
		::setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
		rawWrite("[ENV] XDG_RUNTIME_DIR was not set; using /tmp/xdg\n");
	}
	if (std::getenv("APPID") == nullptr)
	{
		// Apps live in <apps>/<app id>/, so the folder name is the app id.
		char exe[512];
		const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		std::string id = "com.ballslover.mcbetacpp1";
		if (n > 0)
		{
			exe[n] = '\0';
			std::string path(exe);
			const std::size_t slash = path.rfind('/');
			if (slash != std::string::npos && slash > 0)
			{
				const std::size_t prev = path.rfind('/', slash - 1);
				const std::string dir = path.substr(prev == std::string::npos ? 0 : prev + 1,
				                                    slash - (prev == std::string::npos ? 0 : prev + 1));
				if (dir.find('.') != std::string::npos)
					id = dir;
			}
		}
		::setenv("APPID", id.c_str(), 0);
		rawWrite("[ENV] APPID was not set; using ");
		rawWrite(id.c_str());
		rawWrite("\n");
	}
	if (std::getenv("SDL_VIDEODRIVER") == nullptr)
		::setenv("SDL_VIDEODRIVER", "wayland", 0);

	// mcbeta.env next to the executable: KEY=VALUE lines (# starts a comment)
	// for MCBETA_* settings, so they can be changed without an SSH shell when
	// the app is started from the launcher. Real environment variables win.
	{
		char exe[512];
		const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		if (n > 0)
		{
			exe[n] = '\0';
			std::string path(exe);
			const std::size_t slash = path.rfind('/');
			if (slash != std::string::npos)
			{
				path = path.substr(0, slash + 1) + "mcbeta.env";
				if (std::FILE *f = std::fopen(path.c_str(), "r"))
				{
					char line[256];
					while (std::fgets(line, sizeof(line), f) != nullptr)
					{
						std::string text(line);
						while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
							text.pop_back();
						const std::size_t start = text.find_first_not_of(" \t");
						if (start == std::string::npos || text[start] == '#')
							continue;
						text = text.substr(start);
						const std::size_t eq = text.find('=');
						if (eq == std::string::npos || eq == 0 || text.compare(0, 7, "MCBETA_") != 0)
							continue;
						const std::string key = text.substr(0, eq);
						const std::string value = text.substr(eq + 1);
						if (std::getenv(key.c_str()) == nullptr && !value.empty())
						{
							::setenv(key.c_str(), value.c_str(), 0);
							rawWrite("[ENV] mcbeta.env: ");
							rawWrite(key.c_str());
							rawWrite("=");
							rawWrite(value.c_str());
							rawWrite("\n");
						}
					}
					std::fclose(f);
				}
			}
		}
	}
}
}

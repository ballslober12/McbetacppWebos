#define SDL_MAIN_HANDLED
#include "SDL.h"

#include <cstring>
#include <cstdlib>

#include "client/Minecraft.h"
#include "java/System.h"
#include "tools/BlockSmoke.h"
#include "tools/FindingsSmoke.h"
#include "tools/MultiplayerScreenSmoke.h"
#include "tools/NetworkSmoke.h"
#include "tools/RegionIoSmoke.h"
#include "tools/SaveConverterSmoke.h"
#include "tools/TerrainStorageSmoke.h"
#ifdef B173_PGO_STRESS_EMBED
#include "tools/stress/StressHarness.h"
#endif

#include "external/SDLException.h"

#include "lwjgl/GLContext.h"

#ifdef MC_WEBOS
#include "webos/WebOSInput.h"
#include "webos/WebOSLog.h"
#endif

int main(int argc, char *argv[])
{
#ifdef B173_PGO_STRESS_EMBED
	if (argc >= 2 && std::strcmp(argv[1], "--stress") == 0)
		return stress::runCommandLine(argc - 1, argv + 1);
#endif
	if (argc >= 2 && std::strcmp(argv[1], "--block-smoke") == 0)
		return runBlockSmoke();
	if (argc >= 2 && std::strcmp(argv[1], "--findings-smoke") == 0)
		return runFindingsSmoke();
	if (argc >= 2 && std::strcmp(argv[1], "--network-smoke") == 0)
		return runNetworkSmoke();
	if (argc >= 2 && std::strcmp(argv[1], "--region-io-smoke") == 0)
		return runRegionIoSmoke();
	if (argc >= 2 && std::strcmp(argv[1], "--terrain-storage-smoke") == 0)
		return runTerrainStorageSmoke();
	if (argc >= 2 && std::strcmp(argv[1], "--save-converter-smoke") == 0)
		return runSaveConverterSmoke();
	if (argc >= 2 && std::strcmp(argv[1], "--multiplayer-screen-smoke") == 0)
		return runMultiplayerScreenSmoke();
#ifdef MC_WEBOS
	webos::installDiagnostics();
	webos::stage("main: SDL init");
	// webOS ties a Wayland surface to an app through the APPID environment
	// variable (set by the launcher). When started from an SSH shell it is
	// missing and the compositor may never show the window, so provide it.
	if (std::getenv("APPID") == nullptr)
	{
		setenv("APPID", "com.ballslover.mcbetacpp1", 0);
		webos::log("[START] APPID was not set (started outside the launcher); using com.ballslover.mcbetacpp1");
	}
	webos::log("[START] APPID=%s WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s",
	           std::getenv("APPID") ? std::getenv("APPID") : "(null)",
	           std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "(null)",
	           std::getenv("XDG_RUNTIME_DIR") ? std::getenv("XDG_RUNTIME_DIR") : "(null)");
	// The reference 3D webOS application uses the same SDL2 Wayland ABI.
	// Do not let SDL silently select X11/other desktop backends on the TV.
	if (std::getenv("SDL_VIDEODRIVER") == nullptr)
		setenv("SDL_VIDEODRIVER", "wayland", 0);
	webos::log("[START] SDL_VIDEODRIVER=%s", std::getenv("SDL_VIDEODRIVER") != nullptr ? std::getenv("SDL_VIDEODRIVER") : "(null)");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0)
		throw SDLException();
	webos::log("[SDL] video driver: %s", SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "(none)");
#else
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0)
		throw SDLException();
#endif
	lwjgl::GLContext::instantiate();
#ifdef MC_WEBOS
	webos::input::init(argc, argv);
	webos::log("[START] input init complete; entering Minecraft::start");
#endif

	jstring username = u"Player" + String::toString(System::currentTimeMillis() % 1000);
	if (argc >= 2)
		username = String::fromUTF8(argv[1]);

	jstring auth = u"-";
	if (argc >= 3)
		auth = String::fromUTF8(argv[2]);

	if (argc >= 4)
	{
		jstring server = String::fromUTF8(argv[3]);
		Minecraft::startAndConnectTo(&username, &auth, &server);
	}
	else
	{
		Minecraft::start(&username, &auth);
	}

#ifdef MC_WEBOS
	webos::input::shutdown();
#endif
	return 0;
}

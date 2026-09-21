#include "CrashHandler.h"

#include "SDL_messagebox.h"

#ifdef MC_WEBOS
#include "webos/WebOSLog.h"
#endif

namespace CrashHandler
{

void Crash(const std::string &message, const std::string &stackTrace)
{
	std::string text = message + "\n\n" + stackTrace;
#ifdef MC_WEBOS
	// SDL_ShowSimpleMessageBox cannot be shown over the TV's fullscreen
	// Wayland surface: it blocks forever and leaves a black screen with no
	// explanation. Report the error where it can be read instead.
	webos::log("[CRASH] %s", text.c_str());
#else
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Minecraft has crashed!", text.c_str(), nullptr);
#endif
}

}

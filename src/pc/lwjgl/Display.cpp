#include "lwjgl/Display.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "java/String.h"
#include "lwjgl/GLContext.h"
#include "lwjgl/Mouse.h"
#include "lwjgl/Keyboard.h"

#include "external/SDLException.h"
#include "GLTrace.h"

#include "SDL.h"
#include <glad/glad.h>

#ifdef MC_WEBOS
#include "webos/GLES2Compat.h"
#include "webos/WebOSInput.h"
#include "webos/WebOSLog.h"
#endif

namespace lwjgl
{
namespace Display
{

static bool close_requested = false;

static DisplayMode current_display_mode(0, 0);

// Display functions
void setDisplayMode(const DisplayMode &display_mode)
{
	if (!display_mode.isFullscreen())
	{
		SDL_SetWindowSize(GLContext::detail::getWindow(), display_mode.getWidth(), display_mode.getHeight());
	}
	current_display_mode = display_mode;
	setFullscreen(display_mode.isFullscreen());
}

DisplayMode getDisplayMode()
{
	return current_display_mode;
}

void setTitle(const jstring &string)
{
	std::string utf8 = String::toUTF8(string);
	SDL_SetWindowTitle(GLContext::detail::getWindow(), utf8.c_str());
}

void setFullscreen(bool fullscreen)
{
#ifdef MC_WEBOS
	// Like the working 3dsimple demo, do not ask SDL for a fullscreen mode on
	// the TV (the compositor already shows the window full screen). Report the
	// real drawable size as the "fullscreen" display mode instead.
	const char *fsEnv = std::getenv("MCBETA_FULLSCREEN");
	if (fullscreen && !(fsEnv != nullptr && *fsEnv == '1'))
	{
		int w = 0, h = 0;
		SDL_GL_GetDrawableSize(GLContext::detail::getWindow(), &w, &h);
		if (w <= 0 || h <= 0)
			SDL_GetWindowSize(GLContext::detail::getWindow(), &w, &h);
		if (w <= 0 || h <= 0)
		{
			w = 1920;
			h = 1080;
		}
		current_display_mode = DisplayMode(w, h, 32, 60);
		webos::log("[DISPLAY] windowed-fullscreen %dx%d", w, h);
		return;
	}
#endif
	if (SDL_SetWindowFullscreen(GLContext::detail::getWindow(), fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0))
		throw SDLException();
	
	// Update display mode
	if (fullscreen)
	{
		int w, h;
		int freq, bpp;

		SDL_DisplayMode sdl_mode;
		if (SDL_GetWindowDisplayMode(GLContext::detail::getWindow(), &sdl_mode))
			throw SDLException();

		w = sdl_mode.w;
		h = sdl_mode.h;
		freq = sdl_mode.refresh_rate;
		bpp = SDL_BITSPERPIXEL(sdl_mode.format);

		current_display_mode = DisplayMode(w, h, bpp, freq);
	}
	else
	{
		int w, h;
		SDL_GetWindowSize(GLContext::detail::getWindow(), &w, &h);
		current_display_mode = DisplayMode(w, h);
	}
}

bool isCloseRequested()
{
#ifdef MC_WEBOS
	return close_requested || webos::input::terminateRequested();
#else
	return close_requested;
#endif
}

bool isVisible()
{
	auto flags = SDL_GetWindowFlags(GLContext::detail::getWindow());
	return (flags & SDL_WINDOW_SHOWN) != 0;
}

bool isActive()
{
#ifdef MC_WEBOS
	// webOS Wayland does not reliably expose SDL_WINDOW_INPUT_FOCUS for the
	// fullscreen application surface. The TV owns this surface exclusively,
	// so treat an existing window as active.
	return GLContext::detail::getWindow() != nullptr;
#else
	auto flags = SDL_GetWindowFlags(GLContext::detail::getWindow());
	return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
#endif
}

void processMessages()
{
#ifdef MC_WEBOS
	// Remote control input (LGNC callbacks, d-pad cursor movement, held keys).
	webos::input::update();
#endif

	SDL_Event e;
	while (SDL_PollEvent(&e))
	{
#ifdef MC_WEBOS
		if (webos::input::filterSdlEvent(e))
			continue;
#endif
		switch (e.type)
		{
			case SDL_QUIT:
				close_requested = true;
				break;
			case SDL_MOUSEMOTION:
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
			case SDL_MOUSEWHEEL:
				Mouse::detail::pushEvent(e);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
			case SDL_TEXTINPUT:
				Keyboard::detail::pushEvent(e);
				break;
		}
	}

	// Update display mode
	if (!current_display_mode.isFullscreen())
	{
		int w, h;
		SDL_GetWindowSize(GLContext::detail::getWindow(), &w, &h);
		current_display_mode = DisplayMode(w, h);
	}
}

void swapBuffers()
{
#ifdef MC_WEBOS
	// With render scaling on, the frame was drawn into a smaller offscreen
	// target: stretch it to the window first, then draw the cursor on top at
	// full resolution.
	const bool scaled = gles2compat::beginPresent(static_cast<int>(getWidth()), static_cast<int>(getHeight()));

	// Software cursor for the remote: menus and GUI screens only, never in the
	// world. (The compositor draws no cursor for this window.)
	if (webos::input::cursorVisible())
	{
		int_t px = 0, py = 0;
		Mouse::getPointerTopLeft(px, py);
		const float scale = std::max(1.0f, static_cast<float>(getHeight()) / 1080.0f) * 1.6f;
		gles2compat::drawCursor(static_cast<float>(px), static_cast<float>(py), getWidth(), getHeight(), scale);
	}
	if (!scaled)
		gles2compat::makeOpaque();
#endif
#ifdef MC_WEBOS
	// While the webOS menu (or another app) is in front, the compositor stops
	// completing frames and the swap blocks. A swap that takes this long is how
	// we notice we were sent to the background when the TV sends no window
	// event; the game then pauses. MCBETA_BG_SWAP_STALL_MS=0 turns it off.
	static const int stallMs = []() {
		const char *env = std::getenv("MCBETA_BG_SWAP_STALL_MS");
		return env != nullptr ? std::max(0, std::atoi(env)) : 1500;
	}();
	const auto swapStart = std::chrono::steady_clock::now();
#endif
	SDL_GL_SwapWindow(GLContext::detail::getWindow());
#ifdef MC_WEBOS
	if (stallMs > 0)
	{
		const int swapMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - swapStart).count());
		if (swapMs >= stallMs)
			webos::input::notifyPresentStall(swapMs);
	}
	gles2compat::endPresent();
#endif
#if defined(B173_GL_TRACE)
	GLTrace::nextFrame();
#endif
}

void update(bool doProcessMessages)
{
	swapBuffers();
	if (doProcessMessages)
		processMessages();
}

void create(bool hidden)
{
	if (hidden)
		SDL_HideWindow(GLContext::detail::getWindow());
	else
		SDL_ShowWindow(GLContext::detail::getWindow());
}

int_t getX()
{
	int x;
	SDL_GetWindowPosition(GLContext::detail::getWindow(), &x, nullptr);
	return x;
}

int_t getY()
{
	int y;
	SDL_GetWindowPosition(GLContext::detail::getWindow(), nullptr, &y);
	return y;
}

int_t getWidth()
{
	return current_display_mode.getWidth();
}

int_t getHeight()
{
	return current_display_mode.getHeight();
}

}
}

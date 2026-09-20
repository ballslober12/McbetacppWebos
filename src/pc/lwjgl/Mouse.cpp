#include "lwjgl/Mouse.h"

#include <queue>

#include "lwjgl/GLContext.h"
#include "lwjgl/Display.h"

#include "external/SDLException.h"

#include "SDL_video.h"
#include "SDL_mouse.h"

namespace lwjgl
{
namespace Mouse
{

static int_t staging_dx = 0;
static int_t staging_dy = 0;
static int_t staging_dz = 0;

// Pointer state tracked from the pushed events. Desktop builds still read the
// live SDL state; webOS builds use this because part of the input (the LGNC
// remote callbacks) never reaches SDL.
static int_t pointer_x = 0;
static int_t pointer_y = 0; // top-left origin
static unsigned int pointer_buttons = 0; // bit n = LWJGL button n

namespace detail
{

static int_t sdlButtonToLwjgl(Uint8 button)
{
	switch (button)
	{
		case SDL_BUTTON_LEFT: return 0;
		case SDL_BUTTON_RIGHT: return 1;
		case SDL_BUTTON_MIDDLE: return 2;
		case SDL_BUTTON_X1: return 3;
		case SDL_BUTTON_X2: return 4;
		default: return button > 0 ? button - 1 : -1;
	}
}

static Uint8 lwjglButtonToSdl(int_t button)
{
	switch (button)
	{
		case 0: return SDL_BUTTON_LEFT;
		case 1: return SDL_BUTTON_RIGHT;
		case 2: return SDL_BUTTON_MIDDLE;
		case 3: return SDL_BUTTON_X1;
		case 4: return SDL_BUTTON_X2;
		default: return 0;
	}
}

struct Event
{
	Sint8 button, down;
	Sint32 x, y;
	Sint32 xrel, yrel;
	Sint32 wheel;

	Event(Sint8 button = 0, Sint8 down = 0, Sint32 x = 0, Sint32 y = 0, Sint32 xrel = 0, Sint32 yrel = 0, Sint32 wheel = 0)
		: button(button), down(down), x(x), y(y), xrel(xrel), yrel(yrel), wheel(wheel)
	{ }
};

static Event event_current = {};
static std::queue<Event> event_queue;

void pushEvent(const SDL_Event &e)
{
	switch (e.type)
	{
		case SDL_MOUSEMOTION:
			pointer_x = e.motion.x;
			pointer_y = e.motion.y;
			staging_dx += e.motion.xrel;
			staging_dy -= e.motion.yrel;
			event_queue.emplace(-1, 0, e.motion.x, Display::getHeight() - e.motion.y - 1, e.motion.xrel, -e.motion.yrel, 0);
			break;
		case SDL_MOUSEWHEEL:
			event_queue.emplace(-1, 0, e.wheel.mouseX, Display::getHeight() - e.wheel.mouseY - 1, 0, 0, e.wheel.y);
			break;
		case SDL_MOUSEBUTTONDOWN:
			pointer_x = e.button.x;
			pointer_y = e.button.y;
			if (sdlButtonToLwjgl(e.button.button) >= 0)
				pointer_buttons |= 1u << sdlButtonToLwjgl(e.button.button);
			event_queue.emplace(sdlButtonToLwjgl(e.button.button), 1, e.button.x, Display::getHeight() - e.button.y - 1, 0, 0, 0);
			break;
		case SDL_MOUSEBUTTONUP:
			pointer_x = e.button.x;
			pointer_y = e.button.y;
			if (sdlButtonToLwjgl(e.button.button) >= 0)
				pointer_buttons &= ~(1u << sdlButtonToLwjgl(e.button.button));
			event_queue.emplace(sdlButtonToLwjgl(e.button.button), 0, e.button.x, Display::getHeight() - e.button.y - 1, 0, 0, 0);
			break;
	}
}

}


void setCursorPosition(int_t x, int_t y)
{
#ifdef MC_WEBOS
	// The remote pointer cannot be warped; just move the tracked position so
	// the next motion delta starts from here.
	pointer_x = x;
	pointer_y = y;
#else
	SDL_WarpMouseInWindow(GLContext::detail::getWindow(), x, y);
#endif
}

void getPointerTopLeft(int_t &x, int_t &y)
{
	x = pointer_x;
	y = pointer_y;
}

// Event handling
bool next()
{
	if (detail::event_queue.empty())
		return false;
	detail::event_current = detail::event_queue.front();
	detail::event_queue.pop();
	return true;
}

int_t getEventButton()
{
	return detail::event_current.button;
}
bool getEventButtonState()
{
	return detail::event_current.down != 0;
}

int_t getEventDX()
{
	return detail::event_current.xrel;
}
int_t getEventDY()
{
	return detail::event_current.yrel;
}

int_t getEventX()
{
	return detail::event_current.x;
}
int_t getEventY()
{
	return detail::event_current.y;
}

int_t getEventDWheel()
{
	return detail::event_current.wheel;
}

// State
int_t getX()
{
#ifdef MC_WEBOS
	return pointer_x;
#else
	int x;
	SDL_GetMouseState(&x, nullptr);
	return x;
#endif
}

int_t getY()
{
#ifdef MC_WEBOS
	return lwjgl::Display::getHeight() - pointer_y - 1;
#else
	int y;
	SDL_GetMouseState(nullptr, &y);
	return lwjgl::Display::getHeight() - y - 1;
#endif
}

int_t getDX()
{
	int_t result = staging_dx;
	staging_dx = 0;
	return result;
}

int_t getDY()
{
	int_t result = staging_dy;
	staging_dy = 0;
	return result;
}

int_t getDWheel()
{
	int_t result = staging_dz;
	staging_dz = 0;
	return result;
}

bool isButtonDown(int_t button)
{
#ifdef MC_WEBOS
	return button >= 0 && button < 32 && (pointer_buttons & (1u << button)) != 0;
#else
	Uint8 sdlButton = detail::lwjglButtonToSdl(button);
	if (sdlButton == 0)
		return false;
	return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(sdlButton)) != 0;
#endif
}

#ifdef MC_WEBOS
static bool grabbed_flag = false;
#endif

bool isGrabbed()
{
#ifdef MC_WEBOS
	// Not every webOS SDL backend supports relative mode, so remember the request.
	return grabbed_flag;
#else
	return SDL_GetRelativeMouseMode() == SDL_TRUE;
#endif
}

void setGrabbed(bool grabbed)
{
	staging_dx = 0;
	staging_dy = 0;
#ifdef MC_WEBOS
	grabbed_flag = grabbed;
#endif

#ifdef MC_WEBOS
	// The TV compositor does not draw a cursor for the game window; menus get a
	// software cursor (see Display::swapBuffers). Relative mode may be missing
	// in the webOS SDL backend, which is fine because remote input is injected.
	SDL_ShowCursor(SDL_DISABLE);
	SDL_SetRelativeMouseMode(grabbed ? SDL_TRUE : SDL_FALSE);
#else
	if (SDL_ShowCursor(grabbed ? SDL_DISABLE : SDL_ENABLE) < 0)
		throw SDLException();
	SDL_SetRelativeMouseMode(grabbed ? SDL_TRUE : SDL_FALSE);
#endif
}

}
}
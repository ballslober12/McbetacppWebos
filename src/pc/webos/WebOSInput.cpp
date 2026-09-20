#include "webos/WebOSInput.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>

#include "SDL.h"

#include "lwjgl/Display.h"
#include "lwjgl/Keyboard.h"
#include "lwjgl/Mouse.h"
#include "webos/WebOSLog.h"

#ifdef MC_WEBOS_LGNC
#include <linux/input.h>
#include <lgncopenapi/lgnc_system.h>
#endif

namespace webos
{
namespace input
{

namespace
{

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------

// Cursor speed with the d-pad, in pixels per second at 1080 lines: it starts
// slow for precise aiming and ramps up while the key stays held.
const float CURSOR_SPEED_MIN = 350.0f;
const float CURSOR_SPEED_MAX = 1500.0f;
const float CURSOR_RAMP_SECONDS = 0.9f;

// Remote layout (in game):
//   d-pad  = W/A/S/D movement       OK = jump (menus: left click)
//   1 = Esc   2 = inventory (E)   3 = right click   4 = left click
//   5/8 = look up/down   7/9 = look left/right   6/0 = next/previous hotbar slot
// In menus the d-pad moves the on-screen cursor and OK clicks.
// Looking with the d-pad is gone (it is movement now); use 5/7/8/9 or the mouse.

// Looking with the number keys in game, in mouse pixels per second.
const float TURN_SPEED = 900.0f;

// The remote may skip release events; a held key with no refresh for this long
// is considered released.
const double STALE_SECONDS = 0.75;

// SDL mouse events are ignored this long after the last LGNC pointer event so
// a TV that reports the pointer through both paths does not click twice.
const double LGNC_POINTER_PRIORITY_SECONDS = 1.5;

// Enter / Return acts as OK only while the cursor is being driven by the remote.
const double POINTER_RECENT_SECONDS = 3.0;

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

enum Action
{
	A_NONE = 0,
	A_UP,
	A_DOWN,
	A_LEFT,
	A_RIGHT,
	A_OK,
	A_BACK,
	A_NUM0,
	A_NUM1,
	A_NUM2,
	A_NUM3,
	A_NUM4,
	A_NUM5,
	A_NUM6,
	A_NUM7,
	A_NUM8,
	A_NUM9,
	A_COUNT
};

double nowSeconds()
{
	return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

bool g_lgncActive = false;
bool g_terminate = false;
bool g_navMode = false;         // cursor moved with the d-pad since the last typing
double g_lastPointerTime = -100.0;
double g_lastLgncPointerTime = -100.0;
double g_lastUpdate = 0.0;

bool g_held[A_COUNT] = {};
double g_heldSince[A_COUNT] = {};
double g_lastSeen[A_COUNT] = {};

bool g_wasGrabbed = false;
// Number keys as remote buttons (see the layout above). MCBETA_REMOTE_DIGITS=0
// turns this off so a real keyboard's digits work normally.
bool g_remoteDigits = true;
double g_lastDigitIntercept = -100.0;
std::set<SDL_Keycode> g_activeKeys;
bool g_leftDown = false;
bool g_rightDown = false;

float g_turnRemainderX = 0.0f;
float g_turnRemainderY = 0.0f;

// LGNC window scaling (LGNC reports positions in display coordinates).
int g_lgncWidth = 0, g_lgncHeight = 0;
int g_lastLgncX = -1, g_lastLgncY = -1;

// ---------------------------------------------------------------------------
// Event injection (main thread)
// ---------------------------------------------------------------------------

void pointerPosition(int &x, int &y)
{
	int_t px = 0, py = 0;
	lwjgl::Mouse::getPointerTopLeft(px, py);
	x = static_cast<int>(px);
	y = static_cast<int>(py);
}

void injectMotion(int x, int y, int dx, int dy)
{
	SDL_Event e;
	std::memset(&e, 0, sizeof(e));
	e.type = SDL_MOUSEMOTION;
	e.motion.x = x;
	e.motion.y = y;
	e.motion.xrel = dx;
	e.motion.yrel = dy;
	lwjgl::Mouse::detail::pushEvent(e);
}

void injectButton(Uint8 button, bool down)
{
	int x, y;
	pointerPosition(x, y);
	SDL_Event e;
	std::memset(&e, 0, sizeof(e));
	e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	e.button.button = button;
	e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.button.clicks = 1;
	e.button.x = x;
	e.button.y = y;
	lwjgl::Mouse::detail::pushEvent(e);
}

void setLeft(bool down)
{
	if (g_leftDown == down)
		return;
	g_leftDown = down;
	injectButton(SDL_BUTTON_LEFT, down);
}

void setRight(bool down)
{
	if (g_rightDown == down)
		return;
	g_rightDown = down;
	injectButton(SDL_BUTTON_RIGHT, down);
}

void injectWheel(int amount)
{
	int x, y;
	pointerPosition(x, y);
	SDL_Event e;
	std::memset(&e, 0, sizeof(e));
	e.type = SDL_MOUSEWHEEL;
	e.wheel.y = amount;
	e.wheel.mouseX = x;
	e.wheel.mouseY = y;
	lwjgl::Mouse::detail::pushEvent(e);
}

void setKey(SDL_Keycode sym, bool down)
{
	const bool active = g_activeKeys.count(sym) != 0;
	if (down == active)
		return;
	if (down)
		g_activeKeys.insert(sym);
	else
		g_activeKeys.erase(sym);

	SDL_Event e;
	std::memset(&e, 0, sizeof(e));
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.repeat = 0;
	e.key.keysym.sym = sym;
	lwjgl::Keyboard::detail::pushEvent(e);
}

void tapKey(SDL_Keycode sym)
{
	setKey(sym, true);
	setKey(sym, false);
}

void typeDigit(int digit)
{
	const SDL_Keycode sym = static_cast<SDL_Keycode>(SDLK_0 + digit);
	tapKey(sym);

	SDL_Event e;
	std::memset(&e, 0, sizeof(e));
	e.type = SDL_TEXTINPUT;
	e.text.text[0] = static_cast<char>('0' + digit);
	e.text.text[1] = '\0';
	lwjgl::Keyboard::detail::pushEvent(e);
	g_navMode = false;
}

void releaseOutputs()
{
	setLeft(false);
	setRight(false);
	const std::set<SDL_Keycode> keys = g_activeKeys;
	for (SDL_Keycode key : keys)
		setKey(key, false);
	std::memset(g_held, 0, sizeof(g_held));
	g_turnRemainderX = g_turnRemainderY = 0.0f;
}

// ---------------------------------------------------------------------------
// Cursor and pointer
// ---------------------------------------------------------------------------

void clampToWindow(int &x, int &y)
{
	const int w = static_cast<int>(lwjgl::Display::getWidth());
	const int h = static_cast<int>(lwjgl::Display::getHeight());
	x = std::max(0, std::min(x, w > 0 ? w - 1 : 0));
	y = std::max(0, std::min(y, h > 0 ? h - 1 : 0));
}

// Absolute pointer position from the remote (window coordinates).
void pointerTo(int x, int y)
{
	int cx, cy;
	pointerPosition(cx, cy);
	clampToWindow(x, y);
	if (x == cx && y == cy)
		return;
	injectMotion(x, y, x - cx, y - cy);
}

void pointerBy(int dx, int dy)
{
	int cx, cy;
	pointerPosition(cx, cy);
	int x = cx + dx, y = cy + dy;
	clampToWindow(x, y);
	if (x == cx && y == cy)
		return;
	injectMotion(x, y, x - cx, y - cy);
}

// ---------------------------------------------------------------------------
// Actions -> events
// ---------------------------------------------------------------------------

SDL_Keycode gameKeyFor(Action a)
{
	switch (a)
	{
		case A_UP: return SDLK_w;
		case A_DOWN: return SDLK_s;
		case A_LEFT: return SDLK_a;
		case A_RIGHT: return SDLK_d;
		default: return SDLK_UNKNOWN;
	}
}

void onActionChanged(Action a, bool down)
{
	const bool menu = !lwjgl::Mouse::isGrabbed();

	// Buttons with the same meaning in menus and in game.
	switch (a)
	{
		case A_BACK:
		case A_NUM1:
			setKey(SDLK_ESCAPE, down);
			return;
		case A_NUM2:
			setKey(SDLK_e, down); // opens / closes the inventory
			return;
		case A_NUM3:
			if (menu)
				g_lastPointerTime = nowSeconds();
			setRight(down);
			return;
		case A_NUM4:
			if (menu)
				g_lastPointerTime = nowSeconds();
			setLeft(down);
			return;
		case A_OK:
			if (menu)
			{
				g_lastPointerTime = nowSeconds();
				setLeft(down);
			}
			else
			{
				setKey(SDLK_SPACE, down); // jump
			}
			return;
		default:
			break;
	}

	if (menu)
	{
		if (a >= A_UP && a <= A_RIGHT)
			g_navMode = true; // the cursor is moved from update()
		else if (down && a >= A_NUM0 && a <= A_NUM9)
			typeDigit(static_cast<int>(a) - static_cast<int>(A_NUM0));
		return;
	}

	// In game
	switch (a)
	{
		case A_UP:
		case A_DOWN:
		case A_LEFT:
		case A_RIGHT:
			setKey(gameKeyFor(a), down);
			break;
		case A_NUM6:
			if (down)
				injectWheel(-1); // next slot
			break;
		case A_NUM0:
			if (down)
				injectWheel(1); // previous slot
			break;
		default:
			break; // 5/7/8/9 look around, handled continuously in update()
	}
}

void setAction(Action a, bool down)
{
	if (a == A_NONE)
		return;

	const double now = nowSeconds();
	if (down)
		g_lastSeen[a] = now;

	if (g_held[a] == down)
		return;
	g_held[a] = down;
	if (down)
		g_heldSince[a] = now;
	onActionChanged(a, down);
}

void applyContinuousInput(double dt)
{
	const bool menu = !lwjgl::Mouse::isGrabbed();
	const double now = nowSeconds();

	// Forget keys whose release never arrived.
	for (int i = 1; i < A_COUNT; i++)
		if (g_held[i] && now - g_lastSeen[i] > STALE_SECONDS)
			setAction(static_cast<Action>(i), false);

	float mx = 0.0f, my = 0.0f; // direction
	if (g_held[A_LEFT]) mx -= 1.0f;
	if (g_held[A_RIGHT]) mx += 1.0f;
	if (g_held[A_UP] && menu) my -= 1.0f;
	if (g_held[A_DOWN] && menu) my += 1.0f;

	if (menu)
	{
		if (mx == 0.0f && my == 0.0f)
			return;

		// Speed ramps with how long the oldest direction key has been held.
		double since = now;
		for (int a = A_UP; a <= A_RIGHT; a++)
			if (g_held[a])
				since = std::min(since, g_heldSince[a]);
		const float t = std::min(1.0f, static_cast<float>(now - since) / CURSOR_RAMP_SECONDS);
		const float scale = std::max(1.0f, static_cast<float>(lwjgl::Display::getHeight()) / 1080.0f);
		const float speed = (CURSOR_SPEED_MIN + (CURSOR_SPEED_MAX - CURSOR_SPEED_MIN) * t) * scale;

		g_turnRemainderX += mx * speed * static_cast<float>(dt);
		g_turnRemainderY += my * speed * static_cast<float>(dt);
		const int dx = static_cast<int>(g_turnRemainderX);
		const int dy = static_cast<int>(g_turnRemainderY);
		g_turnRemainderX -= static_cast<float>(dx);
		g_turnRemainderY -= static_cast<float>(dy);
		if (dx != 0 || dy != 0)
		{
			g_lastPointerTime = now;
			pointerBy(dx, dy);
		}
		return;
	}

	// In game: look around with relative mouse motion (5/8 up/down, 7/9 left/right).
	float lx = 0.0f;
	float ly = 0.0f;
	if (g_held[A_NUM7]) lx -= 1.0f; // look left
	if (g_held[A_NUM9]) lx += 1.0f; // look right
	if (g_held[A_NUM5]) ly -= 1.0f; // look up
	if (g_held[A_NUM8]) ly += 1.0f; // look down
	if (lx == 0.0f && ly == 0.0f)
		return;

	g_turnRemainderX += lx * TURN_SPEED * static_cast<float>(dt);
	g_turnRemainderY += ly * TURN_SPEED * static_cast<float>(dt);
	const int dx = static_cast<int>(g_turnRemainderX);
	const int dy = static_cast<int>(g_turnRemainderY);
	g_turnRemainderX -= static_cast<float>(dx);
	g_turnRemainderY -= static_cast<float>(dy);
	if (dx != 0 || dy != 0)
	{
		int x, y;
		pointerPosition(x, y);
		injectMotion(x, y, dx, dy);
	}
}

// ---------------------------------------------------------------------------
// SDL keyboard -> actions
// ---------------------------------------------------------------------------

Action sdlRemoteAction(SDL_Keycode sym)
{
	switch (sym)
	{
		case SDLK_UP: return A_UP;
		case SDLK_DOWN: return A_DOWN;
		case SDLK_LEFT: return A_LEFT;
		case SDLK_RIGHT: return A_RIGHT;
		case SDLK_RETURN:
		case SDLK_KP_ENTER: return A_OK;
		case SDLK_AC_BACK: return A_BACK;
		default: break;
	}

	if (!g_remoteDigits)
		return A_NONE;
	switch (sym)
	{
		case SDLK_0: case SDLK_KP_0: return A_NUM0;
		case SDLK_1: case SDLK_KP_1: return A_NUM1;
		case SDLK_2: case SDLK_KP_2: return A_NUM2;
		case SDLK_3: case SDLK_KP_3: return A_NUM3;
		case SDLK_4: case SDLK_KP_4: return A_NUM4;
		case SDLK_5: case SDLK_KP_5: return A_NUM5;
		case SDLK_6: case SDLK_KP_6: return A_NUM6;
		case SDLK_7: case SDLK_KP_7: return A_NUM7;
		case SDLK_8: case SDLK_KP_8: return A_NUM8;
		case SDLK_9: case SDLK_KP_9: return A_NUM9;
		default: return A_NONE;
	}
}

// ---------------------------------------------------------------------------
// LGNC
// ---------------------------------------------------------------------------

struct RawEvent
{
	bool pointer; // false: key
	int x, y;
	unsigned int key;
	int cond;
	int linuxCode; // -1 when LGNC gave no input_event
	bool focusOut;
};

std::mutex g_queueMutex;
std::deque<RawEvent> g_queue;

void enqueue(const RawEvent &e)
{
	std::lock_guard<std::mutex> lock(g_queueMutex);
	if (g_queue.size() < 512)
		g_queue.push_back(e);
}

#ifdef MC_WEBOS_LGNC

Action lgncAction(unsigned int key, int linuxCode)
{
	switch (key)
	{
		case LGNC_REMOTE_PRESS: return A_OK;
		case LGNC_REMOTE_BACK: return A_BACK;
		case LGNC_REMOTE_NUM_1: return A_NUM1;
		case LGNC_REMOTE_NUM_2: return A_NUM2;
		case LGNC_REMOTE_NUM_3: return A_NUM3;
		case LGNC_REMOTE_NUM_4: return A_NUM4;
		case LGNC_REMOTE_NUM_5: return A_NUM5;
		case LGNC_REMOTE_NUM_6: return A_NUM6;
		case LGNC_REMOTE_NUM_7: return A_NUM7;
		case LGNC_REMOTE_NUM_8: return A_NUM8;
		case LGNC_REMOTE_NUM_9: return A_NUM9;
		case LGNC_REMOTE_NUM_10: return A_NUM0;
		default: break;
	}

	const unsigned int code = linuxCode >= 0 ? static_cast<unsigned int>(linuxCode) : key;
	switch (code)
	{
		case KEY_UP: return A_UP;
		case KEY_DOWN: return A_DOWN;
		case KEY_LEFT: return A_LEFT;
		case KEY_RIGHT: return A_RIGHT;
		case KEY_ENTER:
		case KEY_KPENTER:
		case KEY_OK:
		case BTN_LEFT: return A_OK;
		case KEY_ESC:
		case KEY_BACK: return A_BACK;
		case KEY_0: return A_NUM0;
		case KEY_1: return A_NUM1;
		case KEY_2: return A_NUM2;
		case KEY_3: return A_NUM3;
		case KEY_4: return A_NUM4;
		case KEY_5: return A_NUM5;
		case KEY_6: return A_NUM6;
		case KEY_7: return A_NUM7;
		case KEY_8: return A_NUM8;
		case KEY_9: return A_NUM9;
		default: return A_NONE;
	}
}

unsigned int onLgncKey(unsigned int key, LGNC_KEY_COND_T cond, LGNC_ADDITIONAL_INPUT_INFO_T *extra)
{
	static int logged = 0;
	if (logged < 40)
	{
		logged++;
		webos::log("[INPUT] LGNC key=%u cond=%d code=%d", key, static_cast<int>(cond), extra ? static_cast<int>(extra->event.code) : -1);
	}

	RawEvent e = {};
	e.pointer = false;
	e.key = key;
	e.cond = static_cast<int>(cond);
	e.linuxCode = extra ? static_cast<int>(extra->event.code) : -1;
	enqueue(e);
	return lgncAction(key, e.linuxCode) != A_NONE ? LGNC_HANDLED : LGNC_NOT_HANDLED;
}

unsigned int onLgncMouse(int x, int y, unsigned int key, LGNC_KEY_COND_T cond, LGNC_ADDITIONAL_INPUT_INFO_T *extra)
{
	static int logged = 0;
	if (logged < 40)
	{
		logged++;
		webos::log("[INPUT] LGNC mouse x=%d y=%d key=%u cond=%d", x, y, key, static_cast<int>(cond));
	}

	RawEvent e = {};
	e.pointer = true;
	e.x = x;
	e.y = y;
	e.key = key;
	e.cond = static_cast<int>(cond);
	e.linuxCode = extra ? static_cast<int>(extra->event.code) : -1;
	enqueue(e);
	return LGNC_HANDLED;
}

LGNC_STATUS_T onLgncMessage(LGNC_MSG_TYPE_T msg, unsigned int, char *, unsigned short)
{
	webos::log("[INPUT] LGNC message %d", static_cast<int>(msg));
	if (msg == LGNC_MSG_TERMINATE)
		g_terminate = true;
	if (msg == LGNC_MSG_FOCUS_OUT)
	{
		RawEvent e = {};
		e.focusOut = true;
		enqueue(e);
	}
	return LGNC_HANDLED;
}

LGNC_CALLBACKS_T g_callbacks;

#endif // MC_WEBOS_LGNC

void processRaw(const RawEvent &e)
{
#ifdef MC_WEBOS_LGNC
	const double now = nowSeconds();

	if (e.focusOut)
	{
		releaseOutputs();
		return;
	}

	if (e.pointer)
	{
		g_lastLgncPointerTime = now;
		g_lastPointerTime = now;

		// Scale display coordinates to the window.
		const int w = static_cast<int>(lwjgl::Display::getWidth());
		const int h = static_cast<int>(lwjgl::Display::getHeight());
		int x = e.x, y = e.y;
		if (g_lgncWidth > 0 && g_lgncHeight > 0 && w > 0 && h > 0)
		{
			x = static_cast<int>(static_cast<long long>(e.x) * w / g_lgncWidth);
			y = static_cast<int>(static_cast<long long>(e.y) * h / g_lgncHeight);
		}

		if (e.x >= 0 && e.y >= 0)
		{
			if (lwjgl::Mouse::isGrabbed())
			{
				// Absolute remote pointer -> relative look.
				if (g_lastLgncX >= 0)
				{
					int px, py;
					pointerPosition(px, py);
					injectMotion(px, py, x - g_lastLgncX, y - g_lastLgncY);
				}
				g_lastLgncX = x;
				g_lastLgncY = y;
			}
			else
			{
				g_lastLgncX = g_lastLgncY = -1;
				pointerTo(x, y);
			}
		}

		// Buttons
		const bool isLeft = e.key == LGNC_REMOTE_PRESS || e.key == BTN_LEFT || e.linuxCode == BTN_LEFT;
		const bool isRight = e.key == BTN_RIGHT || e.linuxCode == BTN_RIGHT;
		if (e.cond == LGNC_KEY_PRESS || e.cond == LGNC_KEY_REPEAT)
		{
			if (isLeft) setLeft(true);
			else if (isRight) setRight(true);
		}
		else if (e.cond == LGNC_KEY_RELEASE)
		{
			if (isLeft) setLeft(false);
			else if (isRight) setRight(false);
		}
		return;
	}

	const Action a = lgncAction(e.key, e.linuxCode);
	if (a == A_NONE)
		return;
	if (e.cond == LGNC_KEY_PRESS || e.cond == LGNC_KEY_REPEAT)
		setAction(a, true);
	else if (e.cond == LGNC_KEY_RELEASE)
		setAction(a, false);
#else
	(void)e;
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void init(int argc, char **argv)
{
	g_lastUpdate = nowSeconds();
	if (const char *env = std::getenv("MCBETA_REMOTE_DIGITS"))
		g_remoteDigits = !(*env == '0');
	webos::log("[INPUT] remote number keys %s", g_remoteDigits ? "mapped (1=Esc 2=Inventory 3=RightClick 4=LeftClick)" : "passed through");

#ifdef MC_WEBOS_LGNC
	std::memset(&g_callbacks, 0, sizeof(g_callbacks));
	g_callbacks.msgHandler = onLgncMessage;
	g_callbacks.keyEventCallback = onLgncKey;
	g_callbacks.mouseEventCallback = onLgncMouse;
	g_callbacks.joystickEventCallback = nullptr;

	const int rc = LGNC_SYSTEM_Initialize(argc, argv, &g_callbacks);
	if (rc == LGNC_OK)
	{
		g_lgncActive = true;
		int w = 0, h = 0;
		if (LGNC_SYSTEM_GetDisplayResolution(&w, &h) == LGNC_OK && w > 0 && h > 0)
		{
			g_lgncWidth = w;
			g_lgncHeight = h;
		}
		webos::log("[INPUT] LGNC system input active (display %dx%d)", g_lgncWidth, g_lgncHeight);
	}
	else
	{
		webos::log("[INPUT] LGNC_SYSTEM_Initialize returned %d, using SDL input only", rc);
	}
#else
	(void)argc;
	(void)argv;
	webos::log("[INPUT] built without LGNC, using SDL input only");
#endif

	// Start with the cursor in the middle of the screen.
	int w = static_cast<int>(lwjgl::Display::getWidth());
	int h = static_cast<int>(lwjgl::Display::getHeight());
	if (w > 0 && h > 0)
		lwjgl::Mouse::setCursorPosition(w / 2, h / 2);
}

void shutdown()
{
#ifdef MC_WEBOS_LGNC
	if (g_lgncActive)
	{
		LGNC_SYSTEM_Finalize();
		g_lgncActive = false;
	}
#endif
}

void update()
{
	const double now = nowSeconds();
	double dt = now - g_lastUpdate;
	g_lastUpdate = now;
	if (dt < 0.0) dt = 0.0;
	if (dt > 0.1) dt = 0.1;

	// Menus <-> game switch: drop whatever the previous mode was holding.
	const bool grabbed = lwjgl::Mouse::isGrabbed();
	if (grabbed != g_wasGrabbed)
	{
		g_wasGrabbed = grabbed;
		releaseOutputs();
		g_lastLgncX = g_lastLgncY = -1;
		g_navMode = false;
	}

	std::deque<RawEvent> pending;
	{
		std::lock_guard<std::mutex> lock(g_queueMutex);
		pending.swap(g_queue);
	}
	for (const RawEvent &e : pending)
		processRaw(e);

	applyContinuousInput(dt);
}

bool filterSdlEvent(const SDL_Event &e)
{
	const double now = nowSeconds();

	switch (e.type)
	{
		case SDL_MOUSEMOTION:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEWHEEL:
			if (now - g_lastLgncPointerTime < LGNC_POINTER_PRIORITY_SECONDS)
				return true; // LGNC already reports the pointer
			g_lastPointerTime = now;
			if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
			{
				// Keep the injected button bookkeeping in sync with real clicks.
				if (e.button.button == SDL_BUTTON_LEFT)
					g_leftDown = e.type == SDL_MOUSEBUTTONDOWN;
				else if (e.button.button == SDL_BUTTON_RIGHT)
					g_rightDown = e.type == SDL_MOUSEBUTTONDOWN;
			}
			return false;

		case SDL_TEXTINPUT:
			// The digit key that was just turned into a remote button also
			// produces a text event; drop it so it is not typed as well.
			if (g_remoteDigits && now - g_lastDigitIntercept < 0.2 &&
			    e.text.text[0] >= '0' && e.text.text[0] <= '9' && e.text.text[1] == '\0')
				return true;
			g_navMode = false; // someone is typing on a real keyboard
			return false;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
		{
			const Action a = sdlRemoteAction(e.key.keysym.sym);
			if (a == A_NONE)
				return false;

			const bool menu = !lwjgl::Mouse::isGrabbed();
			const bool down = e.type == SDL_KEYDOWN;

			// Digits: 1-4 are remote buttons everywhere; the other digits only
			// in game (in menus they stay ordinary typing).
			if (a >= A_NUM0 && a <= A_NUM9)
			{
				if (menu && (a < A_NUM1 || a > A_NUM4))
					return false;
				g_lastDigitIntercept = now;
				setAction(a, down);
				return true;
			}

			// Return only clicks while the remote drives the cursor, so a real
			// keyboard can still confirm text fields.
			if (a == A_OK && menu && !g_navMode && now - g_lastPointerTime > POINTER_RECENT_SECONDS)
				return false;
			// Escape/Back from SDL is already an Escape key for the game.
			if (a == A_BACK)
				return false;

			setAction(a, down);
			if (down && a >= A_UP && a <= A_RIGHT && menu)
				g_lastPointerTime = now;
			return true;
		}

		default:
			return false;
	}
}

bool terminateRequested()
{
	return g_terminate;
}

bool cursorVisible()
{
	static const bool disabled = true;
	return !disabled && !lwjgl::Mouse::isGrabbed();
}

}
}

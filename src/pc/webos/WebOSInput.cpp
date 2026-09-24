#include "webos/WebOSInput.h"
#include "webos/OnScreenKeyboard.h"
#include "SDL_gamecontroller.h"
#include "SDL_joystick.h"
#include "SDL_hints.h"
#include <map>
#include <cmath>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>

#include <unistd.h>

#include "SDL.h"

#include "client/gui/ScreenSizeCalculator.h"
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
//   5/8 = look up/down   7/9 = look left/right   6 = next hotbar slot   0 = chat
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
std::atomic<bool> g_terminate{false};

// App lifecycle. All of these are touched from signal handlers or SDL callbacks,
// so they are lock-free atomics.
std::atomic<bool> g_bgEvent{false};       // "went to the background", waiting to be consumed
std::atomic<bool> g_backgrounded{false};  // window known hidden / minimised
std::atomic<bool> g_exitArmed{false};     // failsafe alarm already running
bool g_pauseInBackground = true;          // MCBETA_PAUSE_IN_BACKGROUND
int g_exitGraceSeconds = 4;               // MCBETA_EXIT_GRACE (0 = no failsafe)
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

	// On-screen keyboard: while it is up, the d-pad and OK belong to it.
	if (menu && osk::visible() && (a == A_UP || a == A_DOWN || a == A_LEFT || a == A_RIGHT || a == A_OK))
	{
		if (down)
		{
			if (a == A_UP) osk::move(0, -1);
			else if (a == A_DOWN) osk::move(0, 1);
			else if (a == A_LEFT) osk::move(-1, 0);
			else if (a == A_RIGHT) osk::move(1, 0);
			else osk::press();
		}
		return;
	}
	// On a text screen 2 shows/hides the keyboard instead of typing "e".
	if (menu && a == A_NUM2 && osk::available())
	{
		if (down)
			osk::toggle();
		return;
	}

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
				tapKey(SDLK_t); // open chat (default chat key)
			break;
		case A_NUM9:
			if (down)
				tapKey(SDLK_F7); // debug menu (9 also looks right while held, see update())
			break;
		default:
			break; // 5/7/8 look around, handled continuously in update()
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
		if ((mx == 0.0f && my == 0.0f) || osk::visible())
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


// ---------------------------------------------------------------------------
// Gamepad (SDL game controller API; raw joysticks as an Xbox-layout fallback)
//
//  In the world                          In menus / GUI screens / chat
//  ----------------------------------    --------------------------------------
//  Left stick   W/A/S/D                  Left stick   cursor (keyboard keys while
//  Right stick  look around                           the on-screen keyboard is up)
//  A            jump                     Right stick  cursor
//  B            Q (drop item)            A            left click (types the key
//  Y            inventory (E)                         while the keyboard is up)
//  X            -                        B            Escape
//  RT           break block (left click) X            right click
//  LT           place / use (right click) Y            E (closes the inventory)
//  RB / LB      next / previous slot     RT / LT      left / right click
//  Start        Escape                   RB / LB      scroll
//  D-pad up     F3 (debug screen)        D-pad       moves the cursor exactly one inventory
//  D-pad down   -                                    slot per press (repeats while held);
//  D-pad l/r    -                                    keyboard up: moves over the keys;
//                                                    text screens: D-pad down toggles the keyboard
//  L3 (left stick click)  chat (T)       L3           closes the chat
//                                        R3 (hold)    precision cursor (about 1/3 speed)
// ---------------------------------------------------------------------------

enum PadAxis
{
	PAD_LX,
	PAD_LY,
	PAD_RX,
	PAD_RY,
	PAD_LT,
	PAD_RT,
	PAD_AXIS_COUNT
};

const float PAD_STICK_DEADZONE = 0.20f;
// Mouse units per second at full deflection. The game multiplies these by the
// in-game Sensitivity option (100% = 1.0) and 0.15 degrees per unit, so 800
// is about 120 degrees per second at full deflection and default sensitivity.
const float PAD_LOOK_SPEED = 800.0f;
const float PAD_LOOK_CURVE = 1.8f;       // >1: fine control near the centre of the stick
// Pixels per second at 1080 lines, full deflection. This used to be 1500,
// which crossed the whole inventory in half a second and made crafting
// impossible.
const float PAD_CURSOR_SPEED = 620.0f;
const float PAD_CURSOR_CURVE = 2.0f;
const float PAD_CURSOR_PRECISION = 0.35f; // R3 held
// The cursor and camera never advance by more than this many seconds of stick
// input in one frame, so a slow frame cannot make the cursor jump over slots.
const double PAD_MAX_FRAME_SECONDS = 0.05;
// D-pad cursor steps in menus: first repeat after DELAY, then every REPEAT.
const double PAD_STEP_DELAY = 0.40;
const double PAD_STEP_REPEAT = 0.15;

// Optional multipliers (MCBETA_PAD_LOOK / MCBETA_PAD_CURSOR), so the feel can
// be tuned without rebuilding.
float g_padLookScale = 1.0f;
float g_padCursorScale = 1.0f;
// Same multipliers from the in-game Controls screen (Options).
float g_padUserLook = 1.0f;
float g_padUserCursor = 1.0f;

float g_padAxis[PAD_AXIS_COUNT] = {0, 0, 0, 0, 0, 0};
bool g_padButton[SDL_CONTROLLER_BUTTON_MAX] = {false};
std::map<SDL_JoystickID, SDL_GameController *> g_controllers;
std::map<SDL_JoystickID, SDL_Joystick *> g_rawJoysticks;
bool g_padMoveKey[4] = {false, false, false, false}; // W, A, S, D as driven by the stick
bool g_padLeftTrigger = false;
bool g_padRightTrigger = false;
float g_padRemX = 0.0f, g_padRemY = 0.0f;
int g_padNavDir = 0; // keyboard navigation with the left stick: 1 up, 2 down, 3 left, 4 right
double g_padNavNext = 0.0;
int g_padLogBudget = 60;
bool g_padStepPressed[4] = {false, false, false, false};        // up, down, left, right: pressed since the last update
double g_padStepNext[4] = {0.0, 0.0, 0.0, 0.0};                 // next auto-repeat time while held

float normalizeAxis(Sint16 v)
{
	const float f = static_cast<float>(v) / 32767.0f;
	return f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
}

// Radial dead zone with the remaining range rescaled to 0..1.
void applyStickDeadzone(float &x, float &y)
{
	const float mag = std::sqrt(x * x + y * y);
	if (mag < PAD_STICK_DEADZONE)
	{
		x = y = 0.0f;
		return;
	}
	const float scaled = std::min(1.0f, (mag - PAD_STICK_DEADZONE) / (1.0f - PAD_STICK_DEADZONE));
	x = x / mag * scaled;
	y = y / mag * scaled;
}

bool hysteresis(bool current, float value, float on, float off)
{
	return current ? value > off : value > on;
}

void padOpenController(int index)
{
	if (SDL_IsGameController(index))
	{
		SDL_GameController *c = SDL_GameControllerOpen(index);
		if (c == nullptr)
		{
			webos::log("[PAD] cannot open controller %d: %s", index, SDL_GetError());
			return;
		}
		const SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
		if (g_controllers.count(id) == 0)
			webos::log("[PAD] controller connected: %s", SDL_GameControllerName(c) ? SDL_GameControllerName(c) : "(unnamed)");
		g_controllers[id] = c;
	}
	else
	{
		SDL_Joystick *j = SDL_JoystickOpen(index);
		if (j == nullptr)
		{
			webos::log("[PAD] cannot open joystick %d: %s", index, SDL_GetError());
			return;
		}
		const SDL_JoystickID id = SDL_JoystickInstanceID(j);
		char guid[40] = "";
		SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), guid, sizeof(guid));
		if (g_rawJoysticks.count(id) == 0)
			webos::log("[PAD] joystick (no controller mapping, using Xbox layout): %s guid=%s axes=%d buttons=%d hats=%d",
			           SDL_JoystickName(j) ? SDL_JoystickName(j) : "(unnamed)", guid, SDL_JoystickNumAxes(j),
			           SDL_JoystickNumButtons(j), SDL_JoystickNumHats(j));
		g_rawJoysticks[id] = j;
	}
}

void padCloseInstance(SDL_JoystickID id)
{
	auto c = g_controllers.find(id);
	if (c != g_controllers.end())
	{
		webos::log("[PAD] controller disconnected");
		SDL_GameControllerClose(c->second);
		g_controllers.erase(c);
	}
	auto j = g_rawJoysticks.find(id);
	if (j != g_rawJoysticks.end())
	{
		webos::log("[PAD] joystick disconnected");
		SDL_JoystickClose(j->second);
		g_rawJoysticks.erase(j);
	}
	std::memset(g_padAxis, 0, sizeof(g_padAxis));
	std::memset(g_padButton, 0, sizeof(g_padButton));
}

void padInit()
{
	if (const char *env = std::getenv("MCBETA_PAD_LOOK"))
	{
		const float v = static_cast<float>(std::atof(env));
		if (v > 0.05f && v < 10.0f)
			g_padLookScale = v;
	}
	if (const char *env = std::getenv("MCBETA_PAD_CURSOR"))
	{
		const float v = static_cast<float>(std::atof(env));
		if (v > 0.05f && v < 10.0f)
			g_padCursorScale = v;
	}
	webos::log("[PAD] look speed x%.2f, cursor speed x%.2f (MCBETA_PAD_LOOK / MCBETA_PAD_CURSOR)", g_padLookScale, g_padCursorScale);
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); // the TV's focus flag is unreliable
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0)
	{
		webos::log("[PAD] gamepad support unavailable: %s", SDL_GetError());
		return;
	}
	const int count = SDL_NumJoysticks();
	webos::log("[PAD] %d joystick device(s) found at start", count);
	for (int i = 0; i < count; i++)
		padOpenController(i);
}

void padShutdown()
{
	for (auto &c : g_controllers)
		SDL_GameControllerClose(c.second);
	for (auto &j : g_rawJoysticks)
		SDL_JoystickClose(j.second);
	g_controllers.clear();
	g_rawJoysticks.clear();
}

void padStepPress(int dir, double now)
{
	g_padStepPressed[dir] = true;
	g_padStepNext[dir] = now + PAD_STEP_DELAY;
}

// One button press/release, already translated to the Xbox-style enum.
void padButton(SDL_GameControllerButton b, bool down)
{
	if (b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX)
		return;
	g_padButton[b] = down;

	const bool menu = !lwjgl::Mouse::isGrabbed();
	const bool oskUp = menu && osk::visible();
	const double now = nowSeconds();

	switch (b)
	{
		case SDL_CONTROLLER_BUTTON_A:
			if (oskUp)
			{
				if (down)
					osk::press();
			}
			else if (menu)
			{
				g_lastPointerTime = now;
				setLeft(down);
			}
			else
			{
				setKey(SDLK_SPACE, down); // jump
			}
			break;

		case SDL_CONTROLLER_BUTTON_B:
			if (menu)
				setKey(SDLK_ESCAPE, down);
			else
				setKey(SDLK_q, down); // drop item
			break;

		case SDL_CONTROLLER_BUTTON_X:
			if (menu)
			{
				g_lastPointerTime = now;
				setRight(down);
			}
			break;

		case SDL_CONTROLLER_BUTTON_Y:
			// On a text screen "e" would be typed into the field.
			if (!(menu && osk::available()))
				setKey(SDLK_e, down); // inventory
			break;

		case SDL_CONTROLLER_BUTTON_START:
			setKey(SDLK_ESCAPE, down);
			break;

		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			if (down)
				injectWheel(1); // previous slot / scroll up
			break;

		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
			if (down)
				injectWheel(-1); // next slot / scroll down
			break;

		case SDL_CONTROLLER_BUTTON_LEFTSTICK:
			if (down)
			{
				if (!menu)
					tapKey(SDLK_t); // open chat
				else if (osk::isChat())
					tapKey(SDLK_ESCAPE); // close chat
			}
			break;

		case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
			// In a menu, R3 is the existing cursor-precision modifier read
			// elsewhere via g_padButton[]; only treat it as "open debug menu"
			// during actual gameplay so the two don't collide.
			if (down && !menu)
				tapKey(SDLK_F7); // debug menu
			break;

		case SDL_CONTROLLER_BUTTON_DPAD_UP:
			if (down)
			{
				if (oskUp)
					osk::move(0, -1);
				else if (menu)
					padStepPress(0, now); // one slot up
				else
					tapKey(SDLK_F3); // debug screen
			}
			break;

		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			if (down)
			{
				if (menu && osk::available())
					osk::toggle(); // show / hide the on-screen keyboard (text screens)
				else if (menu)
					padStepPress(1, now); // one slot down
			}
			break;

		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			if (down)
			{
				if (oskUp)
					osk::move(-1, 0);
				else if (menu)
					padStepPress(2, now);
			}
			break;

		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			if (down)
			{
				if (oskUp)
					osk::move(1, 0);
				else if (menu)
					padStepPress(3, now);
			}
			break;

		default:
			break;
	}
}

// Raw joystick fallback: assume the common Xbox/xpad layout.
SDL_GameControllerButton rawButtonToPad(int button)
{
	switch (button)
	{
		case 0: return SDL_CONTROLLER_BUTTON_A;
		case 1: return SDL_CONTROLLER_BUTTON_B;
		case 2: return SDL_CONTROLLER_BUTTON_X;
		case 3: return SDL_CONTROLLER_BUTTON_Y;
		case 4: return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
		case 5: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
		case 6: return SDL_CONTROLLER_BUTTON_BACK;
		case 7: return SDL_CONTROLLER_BUTTON_START;
		case 8: return SDL_CONTROLLER_BUTTON_GUIDE;
		case 9: return SDL_CONTROLLER_BUTTON_LEFTSTICK;
		case 10: return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
		default: return SDL_CONTROLLER_BUTTON_INVALID;
	}
}

bool padHandleEvent(const SDL_Event &e)
{
	switch (e.type)
	{
		case SDL_CONTROLLERDEVICEADDED:
			padOpenController(e.cdevice.which);
			return true;
		case SDL_JOYDEVICEADDED:
			if (!SDL_IsGameController(e.jdevice.which))
				padOpenController(e.jdevice.which);
			return true;
		case SDL_CONTROLLERDEVICEREMOVED:
			padCloseInstance(e.cdevice.which);
			return true;
		case SDL_JOYDEVICEREMOVED:
			padCloseInstance(e.jdevice.which);
			return true;
		case SDL_CONTROLLERDEVICEREMAPPED:
			return true;

		case SDL_CONTROLLERAXISMOTION:
		{
			const float v = normalizeAxis(e.caxis.value);
			switch (e.caxis.axis)
			{
				case SDL_CONTROLLER_AXIS_LEFTX: g_padAxis[PAD_LX] = v; break;
				case SDL_CONTROLLER_AXIS_LEFTY: g_padAxis[PAD_LY] = v; break;
				case SDL_CONTROLLER_AXIS_RIGHTX: g_padAxis[PAD_RX] = v; break;
				case SDL_CONTROLLER_AXIS_RIGHTY: g_padAxis[PAD_RY] = v; break;
				case SDL_CONTROLLER_AXIS_TRIGGERLEFT: g_padAxis[PAD_LT] = v; break;
				case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: g_padAxis[PAD_RT] = v; break;
				default: break;
			}
			return true;
		}
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			if (g_padLogBudget > 0)
			{
				g_padLogBudget--;
				webos::log("[PAD] button %d %s", static_cast<int>(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
			}
			padButton(static_cast<SDL_GameControllerButton>(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN);
			return true;

		// Joysticks without a controller mapping (only those we opened raw).
		case SDL_JOYAXISMOTION:
			if (g_rawJoysticks.count(e.jaxis.which) != 0)
			{
				const float v = normalizeAxis(e.jaxis.value);
				const float trigger = (static_cast<float>(e.jaxis.value) + 32768.0f) / 65535.0f;
				switch (e.jaxis.axis)
				{
					case 0: g_padAxis[PAD_LX] = v; break;
					case 1: g_padAxis[PAD_LY] = v; break;
					case 3: g_padAxis[PAD_RX] = v; break;
					case 4: g_padAxis[PAD_RY] = v; break;
					case 2: g_padAxis[PAD_LT] = trigger; break;
					case 5: g_padAxis[PAD_RT] = trigger; break;
					default: break;
				}
			}
			return true;
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			if (g_rawJoysticks.count(e.jbutton.which) != 0)
			{
				if (g_padLogBudget > 0)
				{
					g_padLogBudget--;
					webos::log("[PAD] raw button %d %s", static_cast<int>(e.jbutton.button), e.type == SDL_JOYBUTTONDOWN ? "down" : "up");
				}
				padButton(rawButtonToPad(e.jbutton.button), e.type == SDL_JOYBUTTONDOWN);
			}
			return true;
		case SDL_JOYHATMOTION:
			if (g_rawJoysticks.count(e.jhat.which) != 0)
			{
				const bool up = (e.jhat.value & SDL_HAT_UP) != 0;
				const bool down = (e.jhat.value & SDL_HAT_DOWN) != 0;
				const bool left = (e.jhat.value & SDL_HAT_LEFT) != 0;
				const bool right = (e.jhat.value & SDL_HAT_RIGHT) != 0;
				const struct { SDL_GameControllerButton b; bool now; } hat[4] = {
					{SDL_CONTROLLER_BUTTON_DPAD_UP, up}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, down},
					{SDL_CONTROLLER_BUTTON_DPAD_LEFT, left}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, right}};
				for (const auto &h : hat)
					if (h.now != g_padButton[h.b])
						padButton(h.b, h.now);
			}
			return true;

		default:
			return false;
	}
}

// Called when the game switches between the world and menus: the outputs were
// just released, so forget the digital states derived from the sticks.
void padModeChanged()
{
	for (bool &k : g_padMoveKey)
		k = false;
	g_padLeftTrigger = g_padRightTrigger = false;
	g_padRemX = g_padRemY = 0.0f;
	g_padNavDir = 0;
	for (bool &pressed : g_padStepPressed)
		pressed = false;
}

void padUpdate(double dt)
{
	const double now = nowSeconds();
	const bool menu = !lwjgl::Mouse::isGrabbed();
	const bool oskUp = menu && osk::visible();

	// A slow frame must not turn into a big cursor / camera jump.
	if (dt > PAD_MAX_FRAME_SECONDS)
		dt = PAD_MAX_FRAME_SECONDS;

	float lx = g_padAxis[PAD_LX], ly = g_padAxis[PAD_LY];
	float rx = g_padAxis[PAD_RX], ry = g_padAxis[PAD_RY];
	applyStickDeadzone(lx, ly);
	applyStickDeadzone(rx, ry);

	// Triggers: RT = left click (break), LT = right click (place). Only the
	// trigger's own press/release is forwarded, so clicks coming from other
	// sources (Magic Remote, mouse over VNC, A/X in menus) are not cancelled.
	const bool rightTrigger = hysteresis(g_padRightTrigger, g_padAxis[PAD_RT], 0.5f, 0.3f);
	const bool leftTrigger = hysteresis(g_padLeftTrigger, g_padAxis[PAD_LT], 0.5f, 0.3f);
	if (rightTrigger != g_padRightTrigger)
	{
		g_padRightTrigger = rightTrigger;
		if (menu)
			g_lastPointerTime = now;
		setLeft(rightTrigger);
	}
	if (leftTrigger != g_padLeftTrigger)
	{
		g_padLeftTrigger = leftTrigger;
		if (menu)
			g_lastPointerTime = now;
		setRight(leftTrigger);
	}

	if (!menu)
	{
		// Left stick = W/A/S/D.
		const float value[4] = {-ly, -lx, ly, lx}; // W, A, S, D
		const SDL_Keycode keys[4] = {SDLK_w, SDLK_a, SDLK_s, SDLK_d};
		for (int i = 0; i < 4; i++)
		{
			const bool want = hysteresis(g_padMoveKey[i], value[i], 0.45f, 0.30f);
			if (want != g_padMoveKey[i])
			{
				g_padMoveKey[i] = want;
				setKey(keys[i], want);
			}
		}

		// Right stick = camera.
		const float mag = std::sqrt(rx * rx + ry * ry);
		if (mag > 0.0f)
		{
			const float m = std::min(1.0f, mag);
			const float curve = std::pow(m, PAD_LOOK_CURVE) / mag; // gentle near the centre
			const float speed = PAD_LOOK_SPEED * g_padLookScale * g_padUserLook * static_cast<float>(dt);
			g_padRemX += rx * curve * speed;
			g_padRemY += ry * curve * speed;
			const int dx = static_cast<int>(g_padRemX);
			const int dy = static_cast<int>(g_padRemY);
			g_padRemX -= static_cast<float>(dx);
			g_padRemY -= static_cast<float>(dy);
			if (dx != 0 || dy != 0)
			{
				int x, y;
				pointerPosition(x, y);
				injectMotion(x, y, dx, dy);
			}
		}
		return;
	}

	// Menus, GUI screens and chat: the sticks move the cursor. With the
	// on-screen keyboard up, the left stick walks over the keys instead.
	float cx = rx, cy = ry;
	if (!oskUp)
	{
		cx += lx;
		cy += ly;
	}
	const float mag = std::sqrt(cx * cx + cy * cy);
	if (mag > 0.0f)
	{
		const float m = std::min(1.0f, mag);
		const float curve = std::pow(m, PAD_CURSOR_CURVE);
		const float scale = std::max(1.0f, static_cast<float>(lwjgl::Display::getHeight()) / 1080.0f);
		// R3 held = precision cursor for picking single slots.
		const float precision = g_padButton[SDL_CONTROLLER_BUTTON_RIGHTSTICK] ? PAD_CURSOR_PRECISION : 1.0f;
		const float speed = PAD_CURSOR_SPEED * g_padCursorScale * g_padUserCursor * precision * curve * scale;
		g_padRemX += cx / mag * speed * static_cast<float>(dt);
		g_padRemY += cy / mag * speed * static_cast<float>(dt);
		const int dx = static_cast<int>(g_padRemX);
		const int dy = static_cast<int>(g_padRemY);
		g_padRemX -= static_cast<float>(dx);
		g_padRemY -= static_cast<float>(dy);
		if (dx != 0 || dy != 0)
		{
			g_lastPointerTime = now;
			pointerBy(dx, dy);
		}
	}

	// D-pad = one inventory slot per press (menus and GUI screens, keyboard hidden).
	if (!oskUp)
	{
		const SDL_GameControllerButton stepButton[4] = {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
		                                                SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
		const int stepX[4] = {0, 0, -1, 1};
		const int stepY[4] = {-1, 1, 0, 0};
		const int pitch = static_cast<int>(18 * std::max<int_t>(1, ScreenSizeCalculator::lastScale));
		for (int i = 0; i < 4; i++)
		{
			bool step = false;
			if (g_padStepPressed[i])
			{
				g_padStepPressed[i] = false;
				step = true;
			}
			else if (g_padButton[stepButton[i]] && now >= g_padStepNext[i])
			{
				g_padStepNext[i] = now + PAD_STEP_REPEAT;
				step = true;
			}
			if (step)
			{
				g_lastPointerTime = now;
				pointerBy(stepX[i] * pitch, stepY[i] * pitch);
			}
		}
	}
	else
	{
		for (bool &pressed : g_padStepPressed)
			pressed = false;
	}

	if (oskUp)
	{
		int dir = 0;
		if (std::fabs(lx) > 0.5f || std::fabs(ly) > 0.5f)
		{
			if (std::fabs(ly) >= std::fabs(lx))
				dir = ly < 0.0f ? 1 : 2;
			else
				dir = lx < 0.0f ? 3 : 4;
		}
		if (dir == 0)
		{
			g_padNavDir = 0;
		}
		else if (dir != g_padNavDir || now >= g_padNavNext)
		{
			const bool first = dir != g_padNavDir;
			g_padNavDir = dir;
			g_padNavNext = now + (first ? 0.35 : 0.14);
			osk::move(dir == 3 ? -1 : (dir == 4 ? 1 : 0), dir == 1 ? -1 : (dir == 2 ? 1 : 0));
		}
	}
	else
	{
		g_padNavDir = 0;
	}
}

} // namespace

// ---------------------------------------------------------------------------
// App lifecycle: closing for real, and pausing when the webOS menu opens
// ---------------------------------------------------------------------------
namespace
{

// Ends the process if the normal shutdown does not finish in time. Uses only
// async-signal-safe calls (alarm / _exit), so it also works from a signal
// handler and when the main thread is stuck (for example inside a buffer swap
// that never returns because the compositor no longer shows the window).
void onAlarmSignal(int)
{
	_exit(0);
}

void armExitFailsafe()
{
	if (g_exitGraceSeconds <= 0)
		return;
	if (!g_exitArmed.exchange(true))
		alarm(static_cast<unsigned int>(g_exitGraceSeconds));
}

// SIGTERM is what the launcher / `luna-send ... applicationmanager/close` sends
// a native app. Ask the game loop to leave (it saves the world on the way out)
// and start the failsafe timer.
void onTerminateSignal(int)
{
	g_terminate.store(true);
	armExitFailsafe();
}

void installLifecycleHandlers()
{
	if (const char *env = std::getenv("MCBETA_EXIT_GRACE"))
		g_exitGraceSeconds = std::max(0, std::min(std::atoi(env), 60));
	if (const char *env = std::getenv("MCBETA_PAUSE_IN_BACKGROUND"))
		g_pauseInBackground = !(*env == '0');

	// Installed after SDL_Init, so these replace SDL's own SIGINT/SIGTERM
	// handlers (which only queue an SDL_QUIT that a blocked main thread never sees).
	struct sigaction term;
	std::memset(&term, 0, sizeof(term));
	term.sa_handler = onTerminateSignal;
	sigemptyset(&term.sa_mask);
	sigaction(SIGTERM, &term, nullptr);
	sigaction(SIGINT, &term, nullptr);
	sigaction(SIGHUP, &term, nullptr);

	struct sigaction alrm;
	std::memset(&alrm, 0, sizeof(alrm));
	alrm.sa_handler = onAlarmSignal;
	sigemptyset(&alrm.sa_mask);
	sigaction(SIGALRM, &alrm, nullptr);

	webos::log("[LIFECYCLE] close handlers installed (failsafe %ds, pause in background %s)",
	           g_exitGraceSeconds, g_pauseInBackground ? "on" : "off");
}

// strong = window hidden / minimised (the webOS menu covers the app): also mute
// and idle. Weak = only focus lost: pause, nothing else, because the TV's focus
// events are not reliable enough to justify muting on them.
void enteredBackground(const char *why, bool strong)
{
	if (!g_pauseInBackground)
		return;
	webos::log("[LIFECYCLE] background (%s)%s", why, strong ? " [hidden]" : "");
	g_bgEvent.store(true);
	if (strong)
		g_backgrounded.store(true);
}

void leftBackground(const char *why)
{
	if (g_backgrounded.exchange(false))
		webos::log("[LIFECYCLE] foreground (%s)", why);
}

void onWindowEvent(const SDL_WindowEvent &w)
{
	static int logged = 0;
	const char *name = nullptr;
	switch (w.event)
	{
		case SDL_WINDOWEVENT_SHOWN: name = "SHOWN"; break;
		case SDL_WINDOWEVENT_HIDDEN: name = "HIDDEN"; break;
		case SDL_WINDOWEVENT_EXPOSED: name = "EXPOSED"; break;
		case SDL_WINDOWEVENT_MINIMIZED: name = "MINIMIZED"; break;
		case SDL_WINDOWEVENT_RESTORED: name = "RESTORED"; break;
		case SDL_WINDOWEVENT_FOCUS_GAINED: name = "FOCUS_GAINED"; break;
		case SDL_WINDOWEVENT_FOCUS_LOST: name = "FOCUS_LOST"; break;
		case SDL_WINDOWEVENT_CLOSE: name = "CLOSE"; break;
		default: break;
	}
	if (name == nullptr)
		return;
	if (logged < 100)
	{
		logged++;
		webos::log("[LIFECYCLE] SDL window event %s", name);
	}

	switch (w.event)
	{
		case SDL_WINDOWEVENT_HIDDEN:
		case SDL_WINDOWEVENT_MINIMIZED:
			enteredBackground(name, true);
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			enteredBackground(name, false);
			break;
		case SDL_WINDOWEVENT_SHOWN:
		case SDL_WINDOWEVENT_RESTORED:
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			leftBackground(name);
			break;
		case SDL_WINDOWEVENT_CLOSE:
			g_terminate.store(true);
			armExitFailsafe();
			break;
		default:
			break;
	}
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

	padInit();
	installLifecycleHandlers();

	// Start with the cursor in the middle of the screen.
	int w = static_cast<int>(lwjgl::Display::getWidth());
	int h = static_cast<int>(lwjgl::Display::getHeight());
	if (w > 0 && h > 0)
		lwjgl::Mouse::setCursorPosition(w / 2, h / 2);
}

void shutdown()
{
	padShutdown();
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
		padModeChanged();
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

	padUpdate(dt);
	applyContinuousInput(dt);
}

bool filterSdlEvent(const SDL_Event &e)
{
	const double now = nowSeconds();

	// Any key or pointer input means the app is in front again, even if the
	// TV never sent a "shown" event.
	if (g_backgrounded.load() &&
	    (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION))
		leftBackground("input");

	if (padHandleEvent(e))
		return true;

	switch (e.type)
	{
		case SDL_QUIT:
		case SDL_APP_TERMINATING:
			webos::log("[LIFECYCLE] quit requested (%s)", e.type == SDL_QUIT ? "SDL_QUIT" : "SDL_APP_TERMINATING");
			g_terminate.store(true);
			armExitFailsafe();
			return false; // Display::processMessages still sees SDL_QUIT

		case SDL_APP_WILLENTERBACKGROUND:
		case SDL_APP_DIDENTERBACKGROUND:
			enteredBackground("SDL_APP_ENTERBACKGROUND", true);
			return false;

		case SDL_APP_WILLENTERFOREGROUND:
		case SDL_APP_DIDENTERFOREGROUND:
			leftBackground("SDL_APP_ENTERFOREGROUND");
			return false;

		case SDL_WINDOWEVENT:
			onWindowEvent(e.window);
			return false;

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
			if (a == A_OK && menu && !osk::visible() && !g_navMode && now - g_lastPointerTime > POINTER_RECENT_SECONDS)
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

void setGamepadScales(float look, float cursor)
{
	g_padUserLook = std::max(0.05f, std::min(look, 10.0f));
	g_padUserCursor = std::max(0.05f, std::min(cursor, 10.0f));
}

bool terminateRequested()
{
	return g_terminate.load();
}

bool consumeBackgroundEvent()
{
	return g_bgEvent.exchange(false);
}

bool isBackgrounded()
{
	return g_backgrounded.load();
}

void notifyPresentStall(int ms)
{
	if (!g_pauseInBackground)
		return;
	webos::log("[LIFECYCLE] frame swap blocked for %d ms: the compositor stopped showing the app", ms);
	g_bgEvent.store(true);
}

bool cursorVisible()
{
	// Menus and GUI screens (inventory, chat, ...) get the cursor; in the world
	// (mouse grabbed) there is none.
	return !lwjgl::Mouse::isGrabbed();
}

void injectText(const char *utf8)
{
	if (utf8 == nullptr || *utf8 == '\0')
		return;
	SDL_Event e;
	std::memset(&e, 0, sizeof(e));
	e.type = SDL_TEXTINPUT;
	std::strncpy(e.text.text, utf8, sizeof(e.text.text) - 1);
	lwjgl::Keyboard::detail::pushEvent(e);
}

void injectKeyTap(int sdlKeycode)
{
	tapKey(static_cast<SDL_Keycode>(sdlKeycode));
}

}
}

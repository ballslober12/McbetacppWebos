#pragma once
// TV remote input for the webOS port.
//
// Two sources feed the game:
//  * SDL2's webOS backend, which delivers the Magic Remote pointer as mouse
//    events and the d-pad / OK / Back keys as keyboard events;
//
// Both are turned into the same LWJGL-style mouse/keyboard events the desktop
// build consumes, so the menus, the "create new world" screen and the game
// itself work unchanged:
//
//  Menus (mouse not grabbed)          In game (mouse grabbed)
//  ------------------------------     -------------------------------------
//  pointer / arrow keys: cursor       Up / Down: walk forward / back
//  OK: left click                     Left / Right: turn
//  Back: Escape                       2 / 8: look up / down
//  0-9: type a digit                  OK: attack / break block (left click)
//                                     1: use / place block (right click)
//                                     3: next hotbar slot
//                                     4 or 5: jump      6: inventory
//                                     7 / 9: strafe left / right   0: sneak
//                                     Back: Escape (pause menu)
//
// The cursor itself is drawn by Display::swapBuffers (gles2compat::drawCursor).
#include "SDL_events.h"

namespace webos
{
namespace input
{

// Registers the LGNC callbacks when available. Safe to call when LGNC is not
// compiled in or refuses to start: SDL input then keeps working on its own.
void init(int argc, char **argv);
void shutdown();

// Called once per frame from Display::processMessages().
void update();

// Called for every polled SDL event before the normal dispatch. Returns true
// when the event was consumed here.
bool filterSdlEvent(const SDL_Event &e);

// Gamepad camera / cursor speed multipliers from the in-game Controls screen
// (1.0 = default). Applied on top of MCBETA_PAD_LOOK / MCBETA_PAD_CURSOR.
void setGamepadScales(float look, float cursor);

// True once the system asked the application to quit (SIGTERM / SIGHUP / SIGINT
// from the launcher or `luna-send ... close`, SDL_QUIT, window close, or the
// LGNC terminate message). From that moment a failsafe timer is running: if the
// game has not exited on its own within MCBETA_EXIT_GRACE seconds (default 4)
// the process is ended with _exit(0), so a stuck frame swap or shutdown can
// never leave the app running behind the launcher.
bool terminateRequested();

// App lifecycle (webOS menu / Home button / another app in front).
//
// consumeBackgroundEvent(): true once each time the app was sent to the
// background (window hidden / minimised / focus lost, or the compositor stopped
// presenting frames). The game reacts by pausing and saving the world.
// isBackgrounded(): true while the window is known to be hidden or minimised;
// the game mutes audio and idles until the app is shown again.
// Set MCBETA_PAUSE_IN_BACKGROUND=0 to turn all of this off.
bool consumeBackgroundEvent();
bool isBackgrounded();

// Called by the presenter when a buffer swap blocked for `ms` milliseconds,
// which on webOS means the compositor stopped showing this app.
void notifyPresentStall(int ms);

// Whether the software cursor should be drawn this frame: only in menus and
// GUI screens, never while playing in the world.
bool cursorVisible();

// Used by the on-screen keyboard: type UTF-8 text / tap one key (SDL keycode).
void injectText(const char *utf8);
void injectKeyTap(int sdlKeycode);

}
}

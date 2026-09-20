#pragma once
// TV remote input for the webOS port.
//
// Two sources feed the game:
//  * SDL2's webOS backend, which delivers the Magic Remote pointer as mouse
//    events and the d-pad / OK / Back keys as keyboard events;
//  * the legacy LGNC callbacks (liblgncopenapi), used when the system accepts
//    LGNC_SYSTEM_Initialize (compiled in when MC_WEBOS_LGNC is defined).
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

// True once the system asked the application to quit (LGNC terminate message).
bool terminateRequested();

// Whether the software cursor should be drawn this frame.
bool cursorVisible();

}
}

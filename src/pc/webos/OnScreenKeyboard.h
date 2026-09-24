#pragma once
// Remote-controlled on-screen keyboard for the webOS port.
//
// The webOS userland headers expose no virtual-keyboard API, so the game brings
// its own: a QWERTY grid drawn over the current screen (see
// Screen::renderOnScreenKeyboard) and driven with the d-pad and OK.
//
// It is only used on screens that need text (chat, multiplayer address, world
// name/seed, sign editing, player name). It opens by itself on those screens
// (except Options, where button 2 opens it) and never appears in the world.
//
// This header is the model only (layout, focus, key presses); no GL code.

namespace webos
{
namespace osk
{

enum class KeyKind
{
	Char,
	Backspace,
	Shift,
	Space,
	Enter,
	Hide
};

struct Key
{
	const char *lower;
	const char *upper;
	KeyKind kind;
	int span; // width in key units
};

constexpr int ROW_COUNT = 5;
constexpr int ROW_UNITS = 10; // every row is this many units wide

int keyCount(int row);
const Key &key(int row, int col);
int keyStartUnit(int row, int col);

// The text to show on a key (respects shift).
const char *label(const Key &key);

// State ------------------------------------------------------------------

// Called every frame the screen is rendered. `available` = the current screen
// has a text field, `autoOpen` = open the keyboard when that screen appears.
// Pass available=false when no such screen is open.
void update(bool available, bool autoOpen);

bool available();      // a text screen is open
void setChat(bool chat); // the open text screen is the chat
bool isChat();
bool visible();        // keyboard is on screen and owns the d-pad / OK
void show();
void hide();
void toggle();

int focusRow();
int focusCol();
bool shifted();

// Input ------------------------------------------------------------------
void move(int dx, int dy);
void press(); // activate the focused key

}
}

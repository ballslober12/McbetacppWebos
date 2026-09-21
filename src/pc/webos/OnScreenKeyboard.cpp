#include "webos/OnScreenKeyboard.h"

#include <algorithm>
#include <cstdlib>

#include "webos/WebOSInput.h"

namespace webos
{
namespace osk
{
namespace
{

const Key ROW0[] = {
	{"1", "!", KeyKind::Char, 1}, {"2", "@", KeyKind::Char, 1}, {"3", "#", KeyKind::Char, 1},
	{"4", "$", KeyKind::Char, 1}, {"5", "%", KeyKind::Char, 1}, {"6", "^", KeyKind::Char, 1},
	{"7", "&", KeyKind::Char, 1}, {"8", "*", KeyKind::Char, 1}, {"9", "(", KeyKind::Char, 1},
	{"0", ")", KeyKind::Char, 1}};
const Key ROW1[] = {
	{"q", "Q", KeyKind::Char, 1}, {"w", "W", KeyKind::Char, 1}, {"e", "E", KeyKind::Char, 1},
	{"r", "R", KeyKind::Char, 1}, {"t", "T", KeyKind::Char, 1}, {"y", "Y", KeyKind::Char, 1},
	{"u", "U", KeyKind::Char, 1}, {"i", "I", KeyKind::Char, 1}, {"o", "O", KeyKind::Char, 1},
	{"p", "P", KeyKind::Char, 1}};
const Key ROW2[] = {
	{"a", "A", KeyKind::Char, 1}, {"s", "S", KeyKind::Char, 1}, {"d", "D", KeyKind::Char, 1},
	{"f", "F", KeyKind::Char, 1}, {"g", "G", KeyKind::Char, 1}, {"h", "H", KeyKind::Char, 1},
	{"j", "J", KeyKind::Char, 1}, {"k", "K", KeyKind::Char, 1}, {"l", "L", KeyKind::Char, 1},
	{"<-", "<-", KeyKind::Backspace, 1}};
const Key ROW3[] = {
	{"Aa", "Aa", KeyKind::Shift, 1}, {"z", "Z", KeyKind::Char, 1}, {"x", "X", KeyKind::Char, 1},
	{"c", "C", KeyKind::Char, 1}, {"v", "V", KeyKind::Char, 1}, {"b", "B", KeyKind::Char, 1},
	{"n", "N", KeyKind::Char, 1}, {"m", "M", KeyKind::Char, 1}, {".", ",", KeyKind::Char, 1},
	{"-", "+", KeyKind::Char, 1}};
const Key ROW4[] = {
	{":", ";", KeyKind::Char, 1}, {"_", "_", KeyKind::Char, 1}, {"@", "/", KeyKind::Char, 1},
	{"Space", "Space", KeyKind::Space, 3}, {"Enter", "Enter", KeyKind::Enter, 2},
	{"Done", "Done", KeyKind::Hide, 2}};

struct RowInfo
{
	const Key *keys;
	int count;
};

const RowInfo ROWS[ROW_COUNT] = {
	{ROW0, static_cast<int>(sizeof(ROW0) / sizeof(Key))},
	{ROW1, static_cast<int>(sizeof(ROW1) / sizeof(Key))},
	{ROW2, static_cast<int>(sizeof(ROW2) / sizeof(Key))},
	{ROW3, static_cast<int>(sizeof(ROW3) / sizeof(Key))},
	{ROW4, static_cast<int>(sizeof(ROW4) / sizeof(Key))}};

bool g_available = false;
bool g_visible = false;
bool g_shift = false;
bool g_chat = false;
int g_row = 1;
int g_col = 0;

int centerUnit2(int row, int col)
{
	// Doubled to stay in integers: 2 * (start + span / 2).
	return 2 * keyStartUnit(row, col) + key(row, col).span;
}

}

int keyCount(int row)
{
	return ROWS[std::max(0, std::min(row, ROW_COUNT - 1))].count;
}

const Key &key(int row, int col)
{
	row = std::max(0, std::min(row, ROW_COUNT - 1));
	col = std::max(0, std::min(col, ROWS[row].count - 1));
	return ROWS[row].keys[col];
}

int keyStartUnit(int row, int col)
{
	int unit = 0;
	for (int i = 0; i < col && i < ROWS[row].count; i++)
		unit += ROWS[row].keys[i].span;
	return unit;
}

const char *label(const Key &k)
{
	return (g_shift && k.kind == KeyKind::Char) ? k.upper : k.lower;
}

void update(bool avail, bool autoOpen)
{
	if (avail == g_available)
		return;
	g_available = avail;
	g_visible = avail && autoOpen;
	g_shift = false;
	if (avail)
	{
		g_row = 1;
		g_col = 0;
	}
}

bool available()
{
	return g_available;
}

void setChat(bool chat)
{
	g_chat = chat;
}

bool isChat()
{
	return g_available && g_chat;
}

bool visible()
{
	return g_available && g_visible;
}

void show()
{
	if (g_available)
		g_visible = true;
}

void hide()
{
	g_visible = false;
}

void toggle()
{
	if (g_available)
		g_visible = !g_visible;
}

int focusRow()
{
	return g_row;
}

int focusCol()
{
	return g_col;
}

bool shifted()
{
	return g_shift;
}

void move(int dx, int dy)
{
	if (dy != 0)
	{
		const int center = centerUnit2(g_row, g_col);
		g_row = (g_row + dy + ROW_COUNT) % ROW_COUNT;
		// Land on the key of the new row nearest to where we were.
		int best = 0;
		int bestDistance = 1 << 30;
		for (int c = 0; c < keyCount(g_row); c++)
		{
			const int distance = std::abs(centerUnit2(g_row, c) - center);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = c;
			}
		}
		g_col = best;
	}
	if (dx != 0)
		g_col = (g_col + dx + keyCount(g_row)) % keyCount(g_row);
}

void press()
{
	const Key &k = key(g_row, g_col);
	switch (k.kind)
	{
		case KeyKind::Char:
			webos::input::injectText(label(k));
			g_shift = false; // one-shot shift
			break;
		case KeyKind::Space:
			webos::input::injectText(" ");
			break;
		case KeyKind::Backspace:
			webos::input::injectKeyTap(0x08 /* SDLK_BACKSPACE */);
			break;
		case KeyKind::Enter:
			webos::input::injectKeyTap(0x0D /* SDLK_RETURN */);
			break;
		case KeyKind::Shift:
			g_shift = !g_shift;
			break;
		case KeyKind::Hide:
			g_visible = false;
			break;
	}
}

}
}

#include "client/gui/DebugMenuScreen.h"

#include "client/Minecraft.h"
#include "client/player/LocalPlayer.h"
#include "client/gui/ContainerScreen.h"

#include "world/item/Item.h"
#include "world/item/ItemInstance.h"

namespace
{
	constexpr int_t MIN_ITEM_ID = 1;
	constexpr int_t MAX_ITEM_ID = 999; // covers every Beta 1.7.3 tile + item id

	bool isValidItemId(int_t id)
	{
		if (id < MIN_ITEM_ID || id >= static_cast<int_t>(Item::items.size()))
			return false;
		return Item::items[id] != nullptr;
	}

	// Steps id in the given direction (+1/-1/+10/-10), skipping over unused
	// ids so every press of a stepper button lands on something giveable.
	int_t nextValidItemId(int_t id, int_t step)
	{
		int_t candidate = id;
		for (int_t attempts = 0; attempts < (MAX_ITEM_ID - MIN_ITEM_ID + 1); ++attempts)
		{
			candidate += (step > 0 ? 1 : -1);
			if (candidate > MAX_ITEM_ID) candidate = MIN_ITEM_ID;
			if (candidate < MIN_ITEM_ID) candidate = MAX_ITEM_ID;
			if (isValidItemId(candidate))
				return candidate;
		}
		return id;
	}
}

DebugMenuScreen::DebugMenuScreen(Minecraft &minecraft) : Screen(minecraft)
{
	itemId = nextValidItemId(0, 1);
}

void DebugMenuScreen::init()
{
	buttons.clear();

	int_t cx = width / 2;

	buttons.push_back(Util::make_shared<Button>(1, cx - 152, height / 4 - 10, 20, 20, u"<"));
	buttons.push_back(Util::make_shared<Button>(2, cx - 130, height / 4 - 10, 96, 20, u"item"));
	buttons.push_back(Util::make_shared<Button>(3, cx - 32, height / 4 - 10, 20, 20, u">"));
	buttons[1]->active = false; // display-only label

	buttons.push_back(Util::make_shared<Button>(4, cx - 152, height / 4 + 16, 20, 20, u"<<"));
	buttons.push_back(Util::make_shared<Button>(5, cx - 130, height / 4 + 16, 96, 20, u"x1"));
	buttons.push_back(Util::make_shared<Button>(6, cx - 32, height / 4 + 16, 20, 20, u">>"));
	buttons[4]->active = false; // display-only label

	buttons.push_back(Util::make_shared<Button>(7, cx - 152, height / 4 + 46, 172, 20, u"Give"));

	buttons.push_back(Util::make_shared<Button>(8, cx - 152, height / 4 + 76, 172, 20, u"Fly: OFF"));
	buttons.push_back(Util::make_shared<Button>(9, cx - 152, height / 4 + 100, 172, 20, u"Heal"));

	buttons.push_back(Util::make_shared<Button>(0, cx - 152, height / 4 + 130, 172, 20, u"Close"));

	refreshLabels();
}

jstring DebugMenuScreen::currentItemLabel() const
{
	if (!isValidItemId(itemId))
		return u"(none)";

	ItemInstance stack(itemId, 1, 0);
	return ContainerScreen::getTooltipName(stack) + u" (#" + String::toString((int)itemId) + u")";
}

void DebugMenuScreen::refreshLabels()
{
	if (buttons.size() < 10)
		return;

	buttons[1]->msg = currentItemLabel();
	buttons[4]->msg = u"x" + String::toString((int)giveCount);

	bool flying = minecraft.player != nullptr && minecraft.player->flying;
	buttons[7]->msg = flying ? u"Fly: ON" : u"Fly: OFF";
}

void DebugMenuScreen::render(int_t xm, int_t ym, float a)
{
	renderBackground();
	drawCenteredString(font, u"Debug menu", width / 2, 20, 0xFFFFFF);
	Screen::render(xm, ym, a);
}

void DebugMenuScreen::buttonClicked(Button &button)
{
	switch (button.id)
	{
	case 1: // item -1
		itemId = nextValidItemId(itemId, -1);
		break;
	case 3: // item +1
		itemId = nextValidItemId(itemId, 1);
		break;
	case 4: // count -1 (min 1)
		if (giveCount > 1)
			giveCount--;
		break;
	case 6: // count +1 (cap at a stack)
		if (giveCount < 64)
			giveCount++;
		break;
	case 7: // give
		if (minecraft.player != nullptr && isValidItemId(itemId))
		{
			ItemInstance stack(itemId, giveCount, 0);
			minecraft.player->inventory.add(stack);
		}
		break;
	case 8: // fly toggle
		if (minecraft.player != nullptr)
			minecraft.player->flying = !minecraft.player->flying;
		break;
	case 9: // heal
		if (minecraft.player != nullptr)
			minecraft.player->health = 20;
		break;
	case 0: // close
		minecraft.setScreen(nullptr);
		minecraft.grabMouse();
		return;
	}

	refreshLabels();
}

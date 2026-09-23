#pragma once

#include "client/gui/Screen.h"

class ItemInstance;

class ContainerScreen : public Screen
{
protected:
	explicit ContainerScreen(Minecraft &minecraft);
	static bool shouldClose(bool alive, bool removed);

public:
	// Also used by DebugMenuScreen to label the item it's about to give.
	static jstring getTooltipName(const ItemInstance &stack);

	void tick() override;
};

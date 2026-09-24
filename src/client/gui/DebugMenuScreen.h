#pragma once

#include "client/gui/Screen.h"

// Dev/debug overlay (F7): give yourself any item, toggle fly, heal to full.
// Not part of vanilla Beta 1.7.3 - purely a development convenience.
class DebugMenuScreen : public Screen
{
private:
	int_t itemId = 1;
	int_t giveCount = 1;

	jstring currentItemLabel() const;
	void refreshLabels();

public:
	explicit DebugMenuScreen(Minecraft &minecraft);

	void init() override;
	void render(int_t xm, int_t ym, float a) override;

protected:
	void buttonClicked(Button &button) override;

public:
	bool isPauseScreen() override { return false; }
};

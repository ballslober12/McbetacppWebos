#pragma once

#include <vector>
#include <memory>

#include "client/gui/GuiComponent.h"
#include "client/gui/Button.h"
#include "client/gui/Font.h"

#include "java/Type.h"
#include "java/String.h"

#include "util/Memory.h"

class Minecraft;

class Screen : public GuiComponent
{
protected:
	Minecraft &minecraft;

public:
	int_t width = 0;
	int_t height = 0;

protected:
	std::vector<std::shared_ptr<Button>> buttons;

public:
	bool passEvents = false;

protected:
	Font &font;

	Screen(Minecraft &minecraft);

public:
	virtual void render(int_t xm, int_t ym, float a);

	// Remote / TV support: screens with a text field say so, and the webOS
	// build then offers its on-screen keyboard (see webos/OnScreenKeyboard.h).
	virtual bool wantsTextInput() const { return false; }
	virtual bool isChatScreen() const { return false; }
	// Open the keyboard as soon as the screen appears (false: the player opens it with 2).
	virtual bool textInputAutoOpen() const { return true; }
	// Gap between the keyboard and the bottom edge, in GUI pixels.
	virtual int textInputBottomMargin() const { return 4; }
	// Draws the on-screen keyboard over the screen (no-op outside webOS builds).
	void renderOnScreenKeyboard();

protected:
	virtual void keyPressed(char_t eventCharacter, int_t eventKey);

public:
	jstring getClipboard();
	void setClipboard(const jstring &text);
	virtual void selectNextField();

private:
	std::shared_ptr<Button> clickedButton = nullptr;

protected:
	virtual void mouseClicked(int_t x, int_t y, int_t buttonNum);
	virtual void mouseReleased(int_t x, int_t y, int_t buttonNum);
	virtual void mouseScrolled(int_t x, int_t y, int_t scrollAmount);

	virtual void buttonClicked(Button &button);

public:
	void init(int_t width, int_t height);
	void setSize(int_t width, int_t height);
	virtual void init();

	void updateEvents();
	void mouseEvent();
	void keyboardEvent();

	virtual void tick();
	virtual void removed();
	void renderBackground();
	void renderBackground(int_t vo);
	void renderDirtBackground(int_t vo);

	virtual bool isPauseScreen();

	virtual void confirmResult(bool result, int_t id);
};

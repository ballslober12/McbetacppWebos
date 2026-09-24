#include "client/gui/ControlsScreen.h"

#include "client/Minecraft.h"
#include "client/locale/Language.h"

#include "client/gui/SmallButton.h"
#include "client/gui/SlideButton.h"
#include "client/Options.h"

constexpr int_t ControlsScreen::BUTTON_WIDTH;
constexpr int_t ControlsScreen::ROW_WIDTH;
constexpr int_t ControlsScreen::PAD_LOOK_ID;
constexpr int_t ControlsScreen::PAD_CURSOR_ID;

ControlsScreen::ControlsScreen(Minecraft &minecraft, std::shared_ptr<Screen> lastScreen, Options &options) : Screen(minecraft), options(options)
{
	this->lastScreen = lastScreen;
}

int_t ControlsScreen::getLeftScreenPosition()
{
	return width / 2 - ROW_WIDTH + 5;
}

void ControlsScreen::init()
{
	Language &language = Language::getInstance();

	int_t leftPos = getLeftScreenPosition();
	for (int_t i = 0; i < Util::size(options.keyMappings); i++)
		buttons.push_back(Util::make_shared<SmallButton>(i, leftPos + (i % 2) * ROW_WIDTH, height / 6 + 24 * (i >> 1), BUTTON_WIDTH, 20, options.getKeyMessage(i)));

#ifdef MC_WEBOS
	// Gamepad speeds (25% .. 200%): camera and menu cursor.
	buttons.push_back(Util::make_shared<SlideButton>(PAD_LOOK_ID, width / 2 - 155, height / 6 + 120, &Options::Option::PAD_LOOK,
		options.getMessage(Options::Option::PAD_LOOK), options.getProgressValue(Options::Option::PAD_LOOK)));
	buttons.push_back(Util::make_shared<SlideButton>(PAD_CURSOR_ID, width / 2 + 5, height / 6 + 120, &Options::Option::PAD_CURSOR,
		options.getMessage(Options::Option::PAD_CURSOR), options.getProgressValue(Options::Option::PAD_CURSOR)));
#endif

	buttons.push_back(Util::make_shared<Button>(200, width / 2 - 100, height / 6 + 168, language.getElement(u"gui.done")));
	title = language.getElement(u"controls.title");
}

void ControlsScreen::buttonClicked(Button &button)
{
	for (int_t i = 0; i < Util::size(options.keyMappings); i++)
		buttons[i]->msg = options.getKeyMessage(i);

	if (button.id == PAD_LOOK_ID || button.id == PAD_CURSOR_ID)
	{
		return; // sliders: SlideButton already applied the value
	}

	if (button.id == 200)
	{
		options.save();
		minecraft.setScreen(lastScreen);
	}
	else
	{
		selectedKey = button.id;
		button.msg = u"> " + button.msg + u" <";
	}
}

void ControlsScreen::keyPressed(char_t eventCharacter, int_t eventKey)
{
	if (selectedKey >= 0)
	{
		options.setKey(selectedKey, eventKey);
		buttons[selectedKey]->msg = options.getKeyMessage(selectedKey);
		selectedKey = -1;
	}
	else
	{
		Screen::keyPressed(eventCharacter, eventKey);
	}
}

void ControlsScreen::render(int_t xm, int_t ym, float a)
{
	renderBackground();
	drawCenteredString(font, title, width / 2, 20, 0xFFFFFF);

	int_t leftPos = getLeftScreenPosition();
	for (int_t i = 0; i < Util::size(options.keyMappings); i++)
		drawString(font, options.getKeyDescription(i), leftPos + (i % 2) * ROW_WIDTH + 70 + 6, height / 6 + 24 * (i >> 1) + 7, 0xFFFFFFFF);

	Screen::render(xm, ym, a);
}

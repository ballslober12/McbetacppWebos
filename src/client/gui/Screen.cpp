#include "client/gui/Screen.h"

#include "client/Minecraft.h"
#include "client/renderer/Tesselator.h"

#include "OpenGL.h"

#include "lwjgl/Keyboard.h"
#include "lwjgl/Mouse.h"
#include "SDL.h"

#include <iostream>

Screen::Screen(Minecraft &minecraft) : minecraft(minecraft), font(*minecraft.font)
{

}

void Screen::render(int_t xm, int_t ym, float a)
{
	for (auto &button : buttons)
		button->render(minecraft, xm, ym);
}

void Screen::keyPressed(char_t eventCharacter, int_t eventKey)
{
	if (eventKey == lwjgl::Keyboard::KEY_ESCAPE)
	{
		minecraft.setScreen(nullptr);
		minecraft.grabMouse();
	}
}

jstring Screen::getClipboard()
{
	char *text = SDL_GetClipboardText();
	if (text == nullptr)
		return u"";

	jstring result = String::fromUTF8(text);
	SDL_free(text);
	return result;
}

void Screen::setClipboard(const jstring &text)
{
	SDL_SetClipboardText(String::toUTF8(text).c_str());
}

void Screen::selectNextField()
{

}

void Screen::mouseClicked(int_t x, int_t y, int_t buttonNum)
{
	if (buttonNum == 0)
	{
		for (auto &button : buttons)
		{
			if (button->clicked(minecraft, x, y))
			{
				clickedButton = button;
				buttonClicked(*button);
			}
		}
	}
}

void Screen::mouseReleased(int_t x, int_t y, int_t buttonNum)
{
	if (clickedButton != nullptr && buttonNum == 0)
	{
		clickedButton->released(x, y);
		clickedButton = nullptr;
	}
}

void Screen::mouseScrolled(int_t x, int_t y, int_t scrollAmount)
{

}

void Screen::buttonClicked(Button &button)
{

}

void Screen::init(int_t width, int_t height)
{
	this->width = width;
	this->height = height;
	buttons.clear();
	init();
}

void Screen::setSize(int_t width, int_t height)
{
	this->width = width;
	this->height = height;
}

void Screen::init()
{

}

void Screen::updateEvents()
{
	while (lwjgl::Mouse::next())
		mouseEvent();
	while (lwjgl::Keyboard::next())
		keyboardEvent();
}

void Screen::mouseEvent()
{
	int_t xm = lwjgl::Mouse::getEventX() * width / minecraft.width;
	int_t ym = height - lwjgl::Mouse::getEventY() * height / minecraft.height - 1;
	int_t scrollAmount = lwjgl::Mouse::getEventDWheel();
	if (scrollAmount != 0)
		mouseScrolled(xm, ym, scrollAmount);

	int_t button = lwjgl::Mouse::getEventButton();
	if (button < 0)
		return;

	if (lwjgl::Mouse::getEventButtonState())
		mouseClicked(xm, ym, button);
	else
		mouseReleased(xm, ym, button);
}

void Screen::keyboardEvent()
{
	if (lwjgl::Keyboard::getEventKeyState())
	{
		if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_F11)
		{
			minecraft.toggleFullscreen();
			return;
		}
		keyPressed(lwjgl::Keyboard::getEventCharacter(), lwjgl::Keyboard::getEventKey());
	}
}

void Screen::tick()
{

}

void Screen::removed()
{

}

void Screen::renderBackground()
{
	renderBackground(0);
}

void Screen::renderBackground(int_t vo)
{
	if (minecraft.level != nullptr)
		fillGradient(0, 0, width, height, 0xC0101010, 0xD0101010);
	else
		renderDirtBackground(vo);
}

void Screen::renderDirtBackground(int_t vo)
{
	glDisable(GL_LIGHTING);
	glDisable(GL_FOG);

	Tesselator &t = Tesselator::instance;

	glBindTexture(GL_TEXTURE_2D, minecraft.textures.loadTexture(u"/gui/background.png"));
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	float s = 32.0f;
	t.begin();
	t.color(0x404040);
	t.vertexUV(0.0, height, 0.0, 0.0, (height / s + vo));
	t.vertexUV(width, height, 0.0, (width / s), (height / s + vo));
	t.vertexUV(width, 0.0, 0.0, (width / s), (0 + vo));
	t.vertexUV(0.0, 0.0, 0.0, 0.0, (0 + vo));
	t.end();
}

bool Screen::isPauseScreen()
{
	return true;
}

void Screen::confirmResult(bool result, int_t id)
{

}


#ifdef MC_WEBOS
#include "webos/OnScreenKeyboard.h"
#endif

void Screen::renderOnScreenKeyboard()
{
#ifdef MC_WEBOS
	using namespace webos::osk;

	if (!available())
		return;

	if (!visible())
	{
		// Only a hint; the keyboard is opened with 2 (Options screen).
		const jstring hint = u"2: keyboard";
		drawString(font, hint, width - font.width(hint) - 4, height - 12, 0xFFAAAAAA);
		return;
	}

	const int keyW = 22, keyH = 16, gap = 2, pad = 4;
	const int rowW = ROW_UNITS * (keyW + gap) - gap;
	const int panelW = rowW + pad * 2;
	const int panelH = ROW_COUNT * (keyH + gap) - gap + pad * 2;
	const int panelX = (width - panelW) / 2;
	const int panelY = height - textInputBottomMargin() - panelH;

	glClear(GL_DEPTH_BUFFER_BIT);
	fill(panelX, panelY, panelX + panelW, panelY + panelH, 0xE0101010);

	for (int r = 0; r < ROW_COUNT; r++)
	{
		for (int c = 0; c < keyCount(r); c++)
		{
			const Key &k = key(r, c);
			const int x0 = panelX + pad + keyStartUnit(r, c) * (keyW + gap);
			const int y0 = panelY + pad + r * (keyH + gap);
			const int x1 = x0 + k.span * (keyW + gap) - gap;
			const int y1 = y0 + keyH;

			const bool focused = r == focusRow() && c == focusCol();
			int background = 0xFF3A3A3A;
			if (k.kind == KeyKind::Shift && shifted())
				background = 0xFF3060C0;
			else if (k.kind == KeyKind::Enter || k.kind == KeyKind::Hide)
				background = 0xFF2E5A2E;
			if (focused)
				background = 0xFFC08000;

			fill(x0 - (focused ? 1 : 0), y0 - (focused ? 1 : 0), x1 + (focused ? 1 : 0), y1 + (focused ? 1 : 0), background);

			const char *text = label(k);
			const jstring str(text, text + std::char_traits<char>::length(text));
			drawCenteredString(font, str, (x0 + x1) / 2, y0 + (keyH - 8) / 2, focused ? 0xFFFFFFFF : 0xFFE0E0E0);
		}
	}
#endif
}

#pragma once

#include <string>
#include <array>

#include "java/Type.h"
#include "java/File.h"

#include "lwjgl/Keyboard.h"

class Minecraft;

class KeyMapping
{
public:
	jstring name;
	int_t key;

	KeyMapping(const jstring &name, int_t key)
	{
		this->name = name;
		this->key = key;
	}
};

class Options
{
public:
	struct Option
	{
	private:
		Option() = delete;

	public:
		struct Element
		{
			bool isProgress;
			bool isBoolean;
			const char16_t *captionId;
		};

		friend class Options;

	public:
		static Element MUSIC;
		static Element SOUND;
		static Element INVERT_MOUSE;
		static Element SENSITIVITY;
		static Element RENDER_DISTANCE;
		static Element VIEW_BOBBING;
		static Element ANAGLYPH;
		static Element LIMIT_FRAMERATE;
		static Element GUI_SCALE;
		static Element AMBIENT_OCCLUSION;
		static Element ADVANCED_OPENGL;
		static Element DIFFICULTY;
		static Element GRAPHICS;
		static Element RENDER_SCALE;  // webOS: render resolution (Auto / 100% / 80% / 67% / 50%)
		static Element PAD_LOOK;      // webOS: gamepad camera speed
		static Element PAD_CURSOR;    // webOS: gamepad menu cursor speed

		static constexpr Element *values[16] = {
			&MUSIC, &SOUND, &INVERT_MOUSE, &SENSITIVITY, &RENDER_DISTANCE, &VIEW_BOBBING, &ANAGLYPH, &LIMIT_FRAMERATE, &GUI_SCALE, &AMBIENT_OCCLUSION, &ADVANCED_OPENGL, &DIFFICULTY, &GRAPHICS, &RENDER_SCALE, &PAD_LOOK, &PAD_CURSOR
		};
	};

private:
	static const char16_t *RENDER_DISTANCE_NAMES[4];
	static const char16_t *LIMIT_FRAMERATE_NAMES[3];
	static const char16_t *GUI_SCALE_NAMES[4];
	static const char16_t *DIFFICULTY_NAMES[4];
	static const char16_t *RENDER_SCALE_NAMES[5];

public:
	float music = 1.0f;
	float sound = 1.0f;

	float mouseSensitivity = 0.5f;
	bool invertYMouse = false;

#ifdef MC_WEBOS
	// The TV's SoC is far weaker than a desktop: start on the cheap settings
	// (Short distance, Fast graphics, smooth lighting off, uncapped frame rate).
	int_t viewDistance = 2;
	bool bobView = true;
	bool anaglyph3d = false;
	int_t limitFramerate = 0;
	int_t guiScale = 0;
	bool ambientOcclusion = false;
	bool advancedOpengl = false;
	bool fancyGraphics = false;
	// Bumped when the one-time webOS performance profile has been applied to a
	// saved options.txt that predates it (see Options::open()).
	int_t webosProfile = 0;
	// 0 = Auto, 1..4 = 100% / 80% / 67% / 50% (see gles2compat::setRenderScaleMode)
	int_t renderScale = 1; // native until the offscreen path has been verified on the TV
	// Gamepad speed multipliers, 0.25 .. 2.0 (1.0 = default)
	float padLook = 1.0f;
	float padCursor = 1.0f;
#else
	int_t viewDistance = 0;
	bool bobView = true;
	bool anaglyph3d = false;
	int_t limitFramerate = 1;
	int_t guiScale = 0;
	bool ambientOcclusion = true;
	bool advancedOpengl = false;
	bool fancyGraphics = true;
#endif

	jstring skin = u"Default";

	KeyMapping keyUp = KeyMapping(u"key.forward", lwjgl::Keyboard::KEY_W);
	KeyMapping keyLeft = KeyMapping(u"key.left", lwjgl::Keyboard::KEY_A);
	KeyMapping keyDown = KeyMapping(u"key.back", lwjgl::Keyboard::KEY_S);
	KeyMapping keyRight = KeyMapping(u"key.right", lwjgl::Keyboard::KEY_D);
	KeyMapping keyJump = KeyMapping(u"key.jump", lwjgl::Keyboard::KEY_SPACE);
	KeyMapping keyFog = KeyMapping(u"key.fog", lwjgl::Keyboard::KEY_F);
	KeyMapping keySneak = KeyMapping(u"key.sneak", lwjgl::Keyboard::KEY_LSHIFT);
	KeyMapping keyDrop = KeyMapping(u"key.drop", lwjgl::Keyboard::KEY_Q);
	KeyMapping keyInventory = KeyMapping(u"key.inventory", lwjgl::Keyboard::KEY_E);
	KeyMapping keyChat = KeyMapping(u"key.chat", lwjgl::Keyboard::KEY_T);

	std::array<KeyMapping*, 10> keyMappings = { &keyUp, &keyLeft, &keyDown, &keyRight, &keyJump, &keySneak, &keyDrop, &keyInventory, &keyChat, &keyFog };

protected:
	Minecraft &minecraft;

private:
	std::unique_ptr<File> optionsFile;

public:
	int_t difficulty = 2;

	bool hideGui = false;
	bool thirdPersonView = false;
	bool showDebugInfo = false;
	bool smoothCamera = false;

	jstring username = u"";
	jstring lastMpIp = u"";

	Options(Minecraft &minecraft);
	void open(File *optionsFile);
	void setUsername(const jstring &username);

	jstring getKeyDescription(int_t i);
	jstring getKeyMessage(int_t i);

	void setKey(int_t i, int_t key);
	void set(Option::Element &option, float value);
	void toggle(Option::Element &option, int_t add);

	float getProgressValue(Option::Element &option);
	bool getBooleanValue(Option::Element &option);

	jstring getMessage(Option::Element &option);

	// Pushes the webOS-only settings (render scale, gamepad speeds) to the platform layer.
	void applyPlatformSettings();

	void load();

private:
	float readFloat(const std::string &s);

public:
	void save();
};

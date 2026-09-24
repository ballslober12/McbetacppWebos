#include "client/Options.h"

#include "client/Minecraft.h"
#include "client/User.h"
#include "client/locale/Language.h"

#include "lwjgl/Keyboard.h"

#ifdef MC_WEBOS
#include <cmath>
#include "webos/GLES2Compat.h"
#include "webos/WebOSInput.h"
#endif

constexpr Options::Option::Element *Options::Option::values[];

Options::Option::Element Options::Option::MUSIC = { true, false, u"options.music" };
Options::Option::Element Options::Option::SOUND = { true, false, u"options.sound" };
Options::Option::Element Options::Option::INVERT_MOUSE = { false, true, u"options.invertMouse" };
Options::Option::Element Options::Option::SENSITIVITY = { true, false, u"options.sensitivity" };
Options::Option::Element Options::Option::RENDER_DISTANCE = { false, false, u"options.renderDistance" };
Options::Option::Element Options::Option::VIEW_BOBBING = { false, true, u"options.viewBobbing" };
Options::Option::Element Options::Option::ANAGLYPH = { false, true, u"options.anaglyph" };
Options::Option::Element Options::Option::LIMIT_FRAMERATE = { false, false, u"options.framerateLimit" };
Options::Option::Element Options::Option::GUI_SCALE = { false, false, u"options.guiScale" };
Options::Option::Element Options::Option::AMBIENT_OCCLUSION = { false, true, u"options.ao" };
Options::Option::Element Options::Option::ADVANCED_OPENGL = { false, true, u"options.advancedOpengl" };
Options::Option::Element Options::Option::DIFFICULTY = { false, false, u"options.difficulty" };
Options::Option::Element Options::Option::GRAPHICS = { false, false, u"options.graphics" };
Options::Option::Element Options::Option::RENDER_SCALE = { false, false, u"options.renderScale" };
Options::Option::Element Options::Option::PAD_LOOK = { true, false, u"options.padLook" };
Options::Option::Element Options::Option::PAD_CURSOR = { true, false, u"options.padCursor" };

const char16_t *Options::RENDER_DISTANCE_NAMES[] = {
	u"options.renderDistance.far",
	u"options.renderDistance.normal",
	u"options.renderDistance.short",
	u"options.renderDistance.tiny"
};
const char16_t *Options::LIMIT_FRAMERATE_NAMES[] = {
	u"performance.max",
	u"performance.balanced",
	u"performance.powersaver"
};
const char16_t *Options::GUI_SCALE_NAMES[] = {
	u"options.guiScale.auto",
	u"options.guiScale.small",
	u"options.guiScale.normal",
	u"options.guiScale.large"
};
const char16_t *Options::RENDER_SCALE_NAMES[] = {
	u"options.renderScale.auto",
	u"options.renderScale.100",
	u"options.renderScale.80",
	u"options.renderScale.67",
	u"options.renderScale.50"
};
const char16_t *Options::DIFFICULTY_NAMES[] = {
	u"options.difficulty.peaceful",
	u"options.difficulty.easy",
	u"options.difficulty.normal",
	u"options.difficulty.hard"
};

Options::Options(Minecraft &minecraft) : minecraft(minecraft)
{

}

void Options::open(File *optionsFile)
{
	this->optionsFile.reset(File::open(*optionsFile, u"options.txt"));
	if (minecraft.user != nullptr)
		setUsername(minecraft.user->name);
	load();
#ifdef MC_WEBOS
	// One-time performance profile for options.txt files saved by older builds
	// (which defaulted to Far distance, Fancy graphics and smooth lighting).
	// Only ever makes things cheaper, and only once: whatever the player picks
	// afterwards is kept.
	if (webosProfile < 1)
	{
		if (viewDistance < 2)
			viewDistance = 2;
		ambientOcclusion = false;
		fancyGraphics = false;
		advancedOpengl = false;
		limitFramerate = 0;
		webosProfile = 1;
	}
	if (webosProfile < 2)
	{
		// v23 defaulted Render Scale to Auto and crashed on some TVs: back to
		// native resolution; pick Auto / 67% again in Video Settings to try it.
		renderScale = 1;
		webosProfile = 2;
	}
#endif
	applyPlatformSettings();
	if (minecraft.user != nullptr)
		minecraft.user->name = username;
	save();
}

void Options::setUsername(const jstring &username)
{
	if (!username.empty())
		this->username = username.substr(0, 16);
}

void Options::applyPlatformSettings()
{
#ifdef MC_WEBOS
	gles2compat::setRenderScaleMode(static_cast<int>(renderScale));
	webos::input::setGamepadScales(padLook, padCursor);
#endif
}

#ifdef MC_WEBOS
namespace
{
// Gamepad speed sliders: 0..1 <-> 25%..200% in steps of 5%, so 100% is exact.
const float PAD_SCALE_MIN = 0.25f;
const float PAD_SCALE_MAX = 2.0f;

float padScaleFromProgress(float progress)
{
	const float raw = PAD_SCALE_MIN + progress * (PAD_SCALE_MAX - PAD_SCALE_MIN);
	return std::round(raw * 20.0f) / 20.0f;
}

float padScaleToProgress(float scale)
{
	return (scale - PAD_SCALE_MIN) / (PAD_SCALE_MAX - PAD_SCALE_MIN);
}
}
#endif

jstring Options::getKeyDescription(int_t i)
{
	Language &l = Language::getInstance();
	return l.getElement(keyMappings[i]->name);
}

jstring Options::getKeyMessage(int_t i)
{
	return lwjgl::Keyboard::getKeyName(keyMappings[i]->key);
}

void Options::setKey(int_t i, int_t key)
{
	keyMappings[i]->key = key;
	save();
}

void Options::set(Option::Element &option, float value)
{
	if (&option == &Option::MUSIC)
	{
		music = value;
	}
	else if (&option == &Option::SOUND)
	{
		sound = value;
	}
	else if (&option == &Option::SENSITIVITY)
		mouseSensitivity = value;
#ifdef MC_WEBOS
	else if (&option == &Option::PAD_LOOK)
	{
		padLook = padScaleFromProgress(value);
		applyPlatformSettings();
	}
	else if (&option == &Option::PAD_CURSOR)
	{
		padCursor = padScaleFromProgress(value);
		applyPlatformSettings();
	}
#endif
}

void Options::toggle(Option::Element &option, int_t add)
{
	if (&option == &Option::INVERT_MOUSE)
		invertYMouse = !invertYMouse;
	else if (&option == &Option::RENDER_DISTANCE)
		viewDistance = (viewDistance + add) & 3;
	else if (&option == &Option::VIEW_BOBBING)
		bobView = !bobView;
	else if (&option == &Option::ANAGLYPH)
	{
		anaglyph3d = !anaglyph3d;
		minecraft.textures.reloadAll();
	}
	else if (&option == &Option::LIMIT_FRAMERATE)
		limitFramerate = (limitFramerate + add + 3) % 3;
	else if (&option == &Option::GUI_SCALE)
		guiScale = (guiScale + add) & 3;
	else if (&option == &Option::AMBIENT_OCCLUSION)
	{
		ambientOcclusion = !ambientOcclusion;
		minecraft.levelRenderer.allChanged();
	}
	else if (&option == &Option::ADVANCED_OPENGL)
	{
		advancedOpengl = !advancedOpengl;
		minecraft.levelRenderer.allChanged();
	}
	else if (&option == &Option::DIFFICULTY)
		difficulty = (difficulty + add) & 3;
	else if (&option == &Option::GRAPHICS)
	{
		fancyGraphics = !fancyGraphics;
		minecraft.levelRenderer.allChanged();
	}
#ifdef MC_WEBOS
	else if (&option == &Option::RENDER_SCALE)
	{
		renderScale = (renderScale + add + 5) % 5;
		applyPlatformSettings();
	}
#endif

	save();
}

float Options::getProgressValue(Option::Element &option)
{
	if (&option == &Option::MUSIC)
		return music;
	else if (&option == &Option::SOUND)
		return sound;
	else if (&option == &Option::SENSITIVITY)
		return mouseSensitivity;
#ifdef MC_WEBOS
	else if (&option == &Option::PAD_LOOK)
		return padScaleToProgress(padLook);
	else if (&option == &Option::PAD_CURSOR)
		return padScaleToProgress(padCursor);
#endif
	return 0.0f;
}

bool Options::getBooleanValue(Option::Element &option)
{
	if (&option == &Option::INVERT_MOUSE)
		return invertYMouse;
	else if (&option == &Option::VIEW_BOBBING)
		return bobView;
	else if (&option == &Option::ANAGLYPH)
		return anaglyph3d;
	else if (&option == &Option::AMBIENT_OCCLUSION)
		return ambientOcclusion;
	else if (&option == &Option::ADVANCED_OPENGL)
		return advancedOpengl;
	return false;
}

jstring Options::getMessage(Option::Element &option)
{
	Language &l = Language::getInstance();
	jstring result = l.getElement(option.captionId) + u": ";

	if (option.isProgress)
	{
		float progress = getProgressValue(option);
#ifdef MC_WEBOS
		if (&option == &Option::PAD_LOOK || &option == &Option::PAD_CURSOR)
		{
			const float scale = &option == &Option::PAD_LOOK ? padLook : padCursor;
			return result + String::fromUTF8(std::to_string(static_cast<int_t>(std::lround(scale * 100.0f)))) + u'%';
		}
#endif
		if (&option == &Option::SENSITIVITY)
		{
			if (progress == 0.0f)
				return result + l.getElement(u"options.sensitivity.min");
			else if (progress == 1.0f)
				return result + l.getElement(u"options.sensitivity.max");
			else
				return result + String::fromUTF8(std::to_string(static_cast<int_t>(progress * 200.0f))) + u'%';
		}
		else
		{
			if (progress == 0.0f)
				return result + l.getElement(u"options.off");
			else
				return result + String::fromUTF8(std::to_string(static_cast<int_t>(progress * 100.0f))) + u'%';
		}
	}
	else if (option.isBoolean)
	{
		bool value = getBooleanValue(option);
		if (value)
			return result + l.getElement(u"options.on");
		else
			return result + l.getElement(u"options.off");
	}
	else if (&option == &Option::RENDER_DISTANCE)
	{
		return result + l.getElement(RENDER_DISTANCE_NAMES[viewDistance]);
	}
	else if (&option == &Option::LIMIT_FRAMERATE)
	{
		return result + l.getElement(LIMIT_FRAMERATE_NAMES[limitFramerate]);
	}
#ifdef MC_WEBOS
	else if (&option == &Option::RENDER_SCALE)
	{
		return result + l.getElement(RENDER_SCALE_NAMES[renderScale < 0 || renderScale > 4 ? 0 : renderScale]);
	}
#endif
	else if (&option == &Option::GUI_SCALE)
	{
		return result + l.getElement(GUI_SCALE_NAMES[guiScale]);
	}
	else if (&option == &Option::DIFFICULTY)
	{
		return result + l.getElement(DIFFICULTY_NAMES[difficulty]);
	}
	else if (&option == &Option::GRAPHICS)
	{
		if (fancyGraphics)
			return result + l.getElement(u"options.graphics.fancy");
		else
			return result + l.getElement(u"options.graphics.fast");
	}

	return result;
}

void Options::load()
{
	if (!optionsFile->exists())
		return;

	std::unique_ptr<std::istream> is(optionsFile->toStreamIn());
	if (!is)
		return;
	
	std::string line;
	while (std::getline(*is, line))
	{
		if (line.back() == '\r')
			line.pop_back();

		size_t pos = line.find(':');
		if (pos == std::string::npos)
			continue;

		std::string key = line.substr(0, pos);
		std::string value = line.substr(pos + 1);

		if (key == "music")
			music = readFloat(value);
		if (key == "sound")
			sound = readFloat(value);
		if (key == "mouseSensitivity")
			mouseSensitivity = readFloat(value);
		if (key == "invertYMouse")
			invertYMouse = value == "true";
		if (key == "viewDistance")
			viewDistance = std::stoi(value);
		if (key == "bobView")
			bobView = value == "true";
		if (key == "anaglyph3d")
			anaglyph3d = value == "true";
		if (key == "limitFramerate")
		{
			limitFramerate = static_cast<int_t>(readFloat(value));
			if (limitFramerate < 0) limitFramerate = 0;
			if (limitFramerate > 2) limitFramerate = 2;
		}
		if (key == "guiScale")
		{
			guiScale = std::stoi(value);
			if (guiScale < 0) guiScale = 0;
			if (guiScale > 3) guiScale = 3;
		}
		if (key == "ao")
			ambientOcclusion = value == "true";
		if (key == "advancedOpengl")
			advancedOpengl = value == "true";
		if (key == "difficulty")
			difficulty = std::stoi(value);
		if (key == "fancyGraphics")
			fancyGraphics = value == "true";
#ifdef MC_WEBOS
		if (key == "webosProfile")
			webosProfile = std::stoi(value);
		if (key == "renderScale")
		{
			renderScale = std::stoi(value);
			if (renderScale < 0 || renderScale > 4) renderScale = 0;
		}
		if (key == "padLook")
			padLook = std::max(0.25f, std::min(2.0f, readFloat(value)));
		if (key == "padCursor")
			padCursor = std::max(0.25f, std::min(2.0f, readFloat(value)));
#endif
		if (key == "skin")
			skin = String::fromUTF8(value);
		if (key == "username")
			setUsername(String::fromUTF8(value));
		if (key == "lastServer")
			lastMpIp = String::fromUTF8(value);

		jstring jkey = String::fromUTF8(key);
		for (int_t i = 0; i < keyMappings.size(); i++)
		{
			if (jkey == u"key_" + keyMappings[i]->name)
				keyMappings[i]->key = std::stoi(value);
		}
	}
}

float Options::readFloat(const std::string &s)
{
	if (s == "true")
		return 1.0f;
	else if (s == "false")
		return 0.0f;
	else
		return std::stof(s);
}

void Options::save()
{
	std::unique_ptr<std::ostream> os(optionsFile->toStreamOut());
	if (!os)
		return;

	*os << std::boolalpha;
	*os << "music:" << music << '\n';
	*os << "sound:" << sound << '\n';
	*os << "invertYMouse:" << invertYMouse << '\n';
	*os << "mouseSensitivity:" << mouseSensitivity << '\n';
	*os << "viewDistance:" << viewDistance << '\n';
	*os << "bobView:" << bobView << '\n';
	*os << "anaglyph3d:" << anaglyph3d << '\n';
	*os << "limitFramerate:" << limitFramerate << '\n';
	*os << "guiScale:" << guiScale << '\n';
	*os << "ao:" << ambientOcclusion << '\n';
	*os << "advancedOpengl:" << advancedOpengl << '\n';
	*os << "difficulty:" << difficulty << '\n';
	*os << "fancyGraphics:" << fancyGraphics << '\n';
#ifdef MC_WEBOS
	*os << "webosProfile:" << webosProfile << '\n';
	*os << "renderScale:" << renderScale << '\n';
	*os << "padLook:" << padLook << '\n';
	*os << "padCursor:" << padCursor << '\n';
#endif
	*os << "skin:" << String::toUTF8(skin) << '\n';
	*os << "username:" << String::toUTF8(username) << '\n';
	*os << "lastServer:" << String::toUTF8(lastMpIp) << '\n';

	for (int_t i = 0; i < keyMappings.size(); i++)
		*os << "key_" << String::toUTF8(keyMappings[i]->name) << ':' << keyMappings[i]->key << '\n';
}

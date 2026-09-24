#include "Minecraft.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "SharedConstants.h"

#include "client/renderer/Chunk.h"
#include "client/renderer/Tesselator.h"
#include "client/renderer/TerrainIndexBuffer.h"
#include "client/gui/DeathScreen.h"
#include "client/gui/ConnectingScreen.h"
#include "client/gui/InventoryScreen.h"
#include "client/gui/ScreenSizeCalculator.h"
#include "client/gui/ChatScreen.h"
#include "client/gui/PauseScreen.h"
#include "client/gui/DebugMenuScreen.h"
#include "client/gui/AchievementToast.h"
#include "client/gui/SleepScreen.h"
#include "client/title/TitleScreen.h"
#include "client/title/AlphaPlaceTitleScreen.h"
#include "ClientTarget.h"
#include "client/player/KeyboardInput.h"
#include "client/spc/SPCCommand.h"
#include "client/ScreenShotHelper.h"
#include "client/gui/ConflictWarningScreen.h"
#include "MinecraftException.h"

#include "client/gamemode/SurvivalMode.h"

#include "world/phys/Vec3.h"
#include "world/phys/AABB.h"
#include "world/level/chunk/ChunkCache.h"
#include "world/level/Level.h"
#include "world/level/SaveConverterMcRegion.h"
#include "world/level/Teleporter.h"
#include "nbt/CompoundTag.h"
#include "world/level/tile/Tile.h"
#include "world/level/tile/DirtTile.h"
#include "world/level/tile/GrassTile.h"
#include "world/level/tile/SlabTile.h"
#include "world/level/tile/StoneTile.h"
#include "world/item/Items.h"
#include "world/stats/Achievement.h"
#include "world/stats/AchievementList.h"
#include "world/stats/StatFileWriter.h"
#include "world/stats/StatList.h"

#include "java/System.h"
#include "java/Runtime.h"
#include "java/File.h"
#include "java/String.h"

#include "util/Mth.h"
#include "util/Profiler.h"

#include "lwjgl/Display.h"
#include "lwjgl/Keyboard.h"

#include "CrashHandler.h"
#ifdef MC_WEBOS
#include "pc/webos/WebOSLog.h"
#include "pc/webos/WebOSInput.h"
#include "webos/GLES2Compat.h"
#endif

#ifdef MC_WEBOS
namespace
{
// Per-second timing summary written to the webOS log ([PERF] lines).
struct PerfAccum
{
	double tickMs = 0, renderMs = 0, swapMs = 0, frameMs = 0;
	int frames = 0;
	std::chrono::steady_clock::time_point frameStart;
	std::chrono::steady_clock::time_point mark;
};
PerfAccum g_perf;

inline double perfMs(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{
	return std::chrono::duration<double, std::milli>(b - a).count();
}
}
#endif

#ifdef MC_WEBOS
#define MC_STAGE(text) do { webos::stage(text); webos::log("[MCINIT] " text); } while (0)
#else
#define MC_STAGE(text) do { } while (0)
#endif

const jstring Minecraft::VERSION_STRING = u"Minecraft " + SharedConstants::VERSION_STRING;

std::array<long_t, 512> Minecraft::frameTimes = {};
std::array<long_t, 512> Minecraft::tickTimes = {};
int_t Minecraft::frameTimePos = 0;

std::shared_ptr<File> Minecraft::workDir;

Minecraft::Minecraft(int_t width, int_t height, bool fullscreen)
{
	this->width = width;
	this->height = height;
	this->fullscreen = fullscreen;
}

void Minecraft::onCrash(const std::string &message, const std::exception &e)
{
	CrashHandler::Crash(message + ": " + e.what());
}

void Minecraft::init(std::shared_ptr<File> directory)
{
	MC_STAGE("Tile::initTiles");
	Tile::initTiles();
	MC_STAGE("Items::initItems");
	Items::initItems();
	// Setup LWJGL
	if (fullscreen)
	{
		lwjgl::Display::setFullscreen(true);
		width = lwjgl::Display::getDisplayMode().getWidth();
		height = lwjgl::Display::getDisplayMode().getHeight();
		if (width < 1)
			width = 1;
		if (height < 1)
			height = 1;
	}
	else
	{
		lwjgl::Display::setDisplayMode(lwjgl::DisplayMode(width, height));
	}

	MC_STAGE("display/title");
	lwjgl::Display::setTitle(VERSION_STRING);

	lwjgl::Display::create(unattended);

#ifdef MC_WEBOS
	// Wayland only maps the fullscreen surface (and lets the compositor hand
	// out its buffers) once the app presents a frame and services events.
	// Loading the fonts/textures below can take a while, so show a black
	// frame now instead of leaving the TV on an unmapped surface.
	MC_STAGE("first present");
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	lwjgl::Display::update(true);
	MC_STAGE("first present done");
#endif

	MC_STAGE("working directory");
	workingDirectory = directory != nullptr ? std::move(directory) : getWorkingDirectory();

	if (!unattended)
		options.open(workingDirectory.get());
	if (!unattended)
		texturePackRepository.updateListAndSelect();
	textures.setTileSize();

	MC_STAGE("sound engine");
	soundEngine.init(&options);
	loadAllSounds();

	MC_STAGE("font");
	font = std::make_unique<Font>(options, u"/font/default.png", textures);
	SPCCommand::setMessageFont(font.get());
	StatList::init();
	AchievementList::openInventory->setDescriptionFormatter([this](const jstring &description)
	{
		jstring result = description;
		size_t position = result.find(u"%1$s");
		if (position != jstring::npos)
			result.replace(position, 4, lwjgl::Keyboard::getKeyName(options.keyInventory.key));
		return result;
	});
	statFileWriter = std::make_unique<StatFileWriter>(*user, *workingDirectory);
	achievementToast = std::make_unique<AchievementToast>(*this);

	MC_STAGE("GL startup");
	checkGlError("Pre startup");
	MC_STAGE("GL state setup");

	glEnable(GL_TEXTURE_2D);
	glShadeModel(GL_SMOOTH);
	glClearDepth(1.0);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glCullFace(GL_BACK);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);

	checkGlError("Startup");
	MC_STAGE("GL state done");

	if (!unattended)
	{
		if (serverHost.empty())
		{
			MC_STAGE("title screen: create");
			std::shared_ptr<Screen> titleScreen = createTitleScreen();
			MC_STAGE("title screen: setScreen");
			setScreen(titleScreen);
			MC_STAGE("title screen: ready");
		}
		else
		{
			setScreen(Util::make_shared<ConnectingScreen>(*this, serverHost, serverPort));
		}
	}
}
void Minecraft::renderLoadingScreen()
{
	ScreenSizeCalculator ssc(options, width, height);
	int_t w = ssc.getWidth();
	int_t h = ssc.getHeight();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, w, h, 0.0, 1000.0, 3000.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0f, 0.0f, -2000.0f);

	glViewport(0, 0, width, height);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	
	Tesselator &tesselator = Tesselator::instance;
	
	glDisable(GL_LIGHTING);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_FOG);

	glBindTexture(GL_TEXTURE_2D, textures.loadTexture(u"/title/mojang.png"));

	tesselator.begin();
	tesselator.color(0xFFFFFF);
	tesselator.vertexUV(0.0, height, 0.0, 0.0, 0.0);
	tesselator.vertexUV(width, height, 0.0, 0.0, 0.0);
	tesselator.vertexUV(width, 0.0, 0.0, 0.0, 0.0);
	tesselator.vertexUV(0.0, 0.0, 0.0, 0.0, 0.0);
	tesselator.end();

	short_t gw = 256;
	short_t gh = 256;
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	tesselator.color(0xFFFFFF);
	blit((width / 2 - gw) / 2, (height / 2 - gh) / 2, 0, 0, gw, gh);

	glDisable(GL_LIGHTING);
	glDisable(GL_FOG);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);

	lwjgl::Display::swapBuffers();
}

void Minecraft::blit(int_t x, int_t y, int_t sx, int_t sy, int_t w, int_t h)
{
	const float us = 1.0f / 256.0f;
	const float vs = 1.0f / 256.0f;
	Tesselator &t = Tesselator::instance;
	t.begin();
	t.vertexUV(x + 0, y + h, 0.0, (sx + 0) * us, (sy + h) * vs);
	t.vertexUV(x + w, y + h, 0.0, (sx + w) * us, (sy + h) * vs);
	t.vertexUV(x + w, y + 0, 0.0, (sx + w) * us, (sy + 0) * vs);
	t.vertexUV(x + 0, y + 0, 0.0, (sx + 0) * us, (sy + 0) * vs);
	t.end();
}

const std::shared_ptr<File> &Minecraft::getWorkingDirectory()
{
	if (workDir == nullptr)
	{
		workDir.reset(File::openWorkingDirectory(u".mcbetacpp"));
		if (!workDir->exists() && !workDir->mkdirs())
			throw std::runtime_error(String::toUTF8(u"The working directory could not be created: " + workDir->toString()));
	}

	return workDir;
}

// mirrors Minecraft::fileDownloaded's category routing (sound/newsound -> sound
// effects, music/newmusic -> background tracks), just done as one synchronous
// disk scan at startup instead of per-file download callbacks.
void Minecraft::loadAllSounds()
{
	std::unique_ptr<File> resourceDir(File::openResourceDirectory());
	loadAllSoundsRecursive(resourceDir.get(), u"");
}

void Minecraft::loadAllSoundsRecursive(File *dir, const jstring &prefix)
{
	if (!dir || !dir->exists() || !dir->isDirectory())
		return;

	auto files = dir->listFiles();
	for (auto &file : files)
	{
		if (file->isDirectory())
		{
			loadAllSoundsRecursive(file.get(), prefix + file->getName() + u"/");
			continue;
		}

		jstring name = file->getName();
		if (name.length() < 4)
			continue;
		jstring ext = name.substr(name.length() - 4);
		if (ext != u".ogg" && ext != u".mus" && ext != u".wav")
			continue;

		jstring fullName = prefix + name;
		size_t slashPos = fullName.find(u'/');
		if (slashPos == jstring::npos)
			continue;
		jstring category = fullName.substr(0, slashPos);
		jstring soundName = fullName.substr(slashPos + 1);
		std::string path = String::toUTF8(file->toString());

		if (category == u"sound" || category == u"newsound")
			soundEngine.add(soundName, path);
		else if (category == u"music" || category == u"newmusic")
			soundEngine.addMusic(soundName, path);
		else if (category == u"streaming")
			soundEngine.addStreaming(soundName, path);
	}
}

std::shared_ptr<Screen> Minecraft::createTitleScreen()
{
	if (ClientTarget::isAlphaPlace())
		return Util::make_shared<AlphaPlaceTitleScreen>(*this);
	return Util::make_shared<TitleScreen>(*this);
}

void Minecraft::setScreen(std::shared_ptr<Screen> screen)
{
	if (this->screen != nullptr)
		this->screen->removed();
	if (statFileWriter != nullptr)
		statFileWriter->syncStats();

	if (screen == nullptr && level == nullptr)
		screen = createTitleScreen();
	else if (screen == nullptr && player != nullptr && player->health <= 0)
		screen = Util::make_shared<DeathScreen>(*this);
	if (std::dynamic_pointer_cast<TitleScreen>(screen) != nullptr ||
		std::dynamic_pointer_cast<AlphaPlaceTitleScreen>(screen) != nullptr)
		SPCCommand::messages.clear();

	this->screen = std::move(screen);
	if (this->screen != nullptr)
	{
		releaseMouse();
		ScreenSizeCalculator ssc(options, width, height);
		int_t w = ssc.getWidth();
		int_t h = ssc.getHeight();
		this->screen->init(w, h);
		noRender = false;
	}
	else
	{
		grabMouse();
	}
}

void Minecraft::checkGlError(const std::string &at)
{
#ifdef MC_WEBOS
	// glGetError can force a driver sync on TV GPUs and this runs twice a frame.
	// MCBETA_GL_CHECK=1 turns the checks back on for debugging.
	static const bool enabled = std::getenv("MCBETA_GL_CHECK") != nullptr;
	if (!enabled)
		return;
#endif
	GLenum error_code = glGetError();
	if (error_code != GL_NO_ERROR)
	{
		std::string error_string;
		switch (error_code)
		{
			default:
				error_string = "unknown error code";
				break;
			case GL_NO_ERROR:
				error_string = "no error";
				break;
			case GL_INVALID_ENUM:
				error_string = "invalid enumerant";
				break;
			case GL_INVALID_VALUE:
				error_string = "invalid value";
				break;
			case GL_INVALID_OPERATION:
				error_string = "invalid operation";
				break;
			case GL_STACK_OVERFLOW:
				error_string = "stack overflow";
				break;
			case GL_STACK_UNDERFLOW:
				error_string = "stack underflow";
				break;
			case GL_OUT_OF_MEMORY:
				error_string = "out of memory";
				break;
		}

		std::cout << "########## GL ERROR ##########\n";
		std::cout << "@ " << at << '\n';
		std::cout << error_code << ": " << error_string << '\n';
	}
}

Minecraft::~Minecraft()
{
	soundEngine.destroy();
	SPCCommand::setMessageFont(nullptr);
	for (std::thread &thread : connectionThreads)
	{
		if (thread.joinable())
			thread.join();
	}
	if (statFileWriter != nullptr)
	{
		statFileWriter->syncStats();
		statFileWriter.reset();
	}
	achievementToast.reset();
	TerrainIndexBuffer::release();
	MemoryTracker::release();
}

void Minecraft::generateFlyby()
{
	gameMode = Util::make_shared<SurvivalMode>(*this);
	selectLevel(u"flyby");
	setScreen(nullptr);

	double player_y = 0.0;

	// Prepare to create a TGA file
	std::vector<char> image_data(width * height * 3);
	std::unique_ptr<File> flyby_file(File::open(*workingDirectory, u"flyby"));
	flyby_file->mkdir();

	char header[18] = {};
	header[2] = 2;
	header[12] = width & 0xFF;
	header[13] = (width >> 8) & 0xFF;
	header[14] = height & 0xFF;
	header[15] = (height >> 8) & 0xFF;
	header[16] = 24;

	// Setup flyby
	int_t frame = -20;
	int_t seconds = 60 * 5 + 52;
	int_t frames = seconds * 60;

	player->yRot = player->yRotO = 12.0f;
	double sin = -std::sin(player->yRot * Mth::PI / 180.0);
	double cos = std::cos(player->yRot * Mth::PI / 180.0);

	player->x = player->xo = player->xOld = 0.0;
	player->z = player->zo = player->zOld = 0.0;

	level->time = 0;

	for (; frame < frames; frame++)
	{
		if ((frame % 100) == 0)
		{
			std::cout << (static_cast<double>(frame) * 100.0 / static_cast<double>(frames)) << "%, free: " << (float)(Runtime::getRuntime().freeMemory() / 1024) / 1024.0f << " MB\n";
		}

		double speed = 0.125 + (static_cast<double>(frame) / static_cast<double>(frames)) * 5.0;

		// Tick world
		AABB::resetPool();
		Vec3::resetPool();

		if (frame < 0)
		{
			level->setSpawnSettings(options.difficulty > 0, true);
			level->tick();
		}

		gameRenderer.tick();

		glEnable(GL_TEXTURE_2D);

		while (level->updateLights());

		// Move player
		player->x = player->xo = player->xOld += sin * speed;
		player->z = player->zo = player->zOld += cos * speed;

		int_t check_distance = 100;
		double min_height = 0.0;
		double check_step = 1.0;

		for (double d = -4.0; d < check_distance; d += check_step)
		{
			for (int_t i = 0; i < 9; i++)
			{
				double x = ((i % 3) / 2.0f) - 0.5;
				double z = ((i / 3) / 2.0f) - 0.5;
				double y = level->getHeightmap(Mth::floor(player->x + sin * d + x), Mth::floor(player->z + cos * d + z));
				if (y > min_height)
					min_height = y;
			}
		}

		double target_y = min_height + 4.0;
		if (player_y == 0.0)
			player_y = target_y;
		else
			player_y += (target_y - player_y) * speed / check_distance * 4.0;

		player->xRot = player->xRotO = static_cast<float>((player_y - 64.0) / 2.0);
		player->y = player->yo = player->yOld = player_y;

		// Render world
		gameRenderer.renderLevel(1.0f, 0);

		lwjgl::Display::update();

		// Save to TGA
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, width, height, GL_BGR_EXT, GL_UNSIGNED_BYTE, image_data.data());

		if (frame >= 0)
		{
			jstring name = String::toString(frame);
			for (; name.size() < 6; name = u"0" + name);

			std::unique_ptr<File> file(File::open(*flyby_file, name + u".tga"));
			std::unique_ptr<std::ostream> out(file->toStreamOut());
			out->write(header, 18);
			out->write(image_data.data(), image_data.size());
		}
	}
}

void Minecraft::run()
{
	// init
	running = true;

#ifdef NDEBUG
	try
	{
#endif
		init();
#ifdef NDEBUG
	}
	catch (std::exception &e)
	{
		onCrash("Failed to start game", e);
		return;
	}
	catch (...)
	{
		CrashHandler::Crash("Failed to start game: unknown exception");
		return;
	}
#endif
	MC_STAGE("init complete, entering main loop");

#ifdef NDEBUG
	try
	{
#endif
		if (FLYBY_MODE)
			generateFlyby();

		long_t fpsMs = System::currentTimeMillis();
		int_t fpsFrames = 0;
		int_t fpsTicks = 0;
#ifdef MC_WEBOS
		bool bgAudioSuspended = false;
		auto lastBackgroundSave = std::chrono::steady_clock::time_point{};
#endif

		while (running)
		{
#ifdef MC_WEBOS
			webos::stage("main loop: tick");
#endif
			Profiler::Scope frameProfile(Profiler::Section::Frame);
			AABB::resetPool();
			Vec3::resetPool();

			if (lwjgl::Display::isCloseRequested())
				stop();

#ifdef MC_WEBOS
			{
				// The app was sent to the background (webOS menu, Home, another
				// app): pause the game and save the world quietly, so nothing is
				// lost if the launcher kills the app while it is out of sight.
				if (webos::input::consumeBackgroundEvent() && level != nullptr)
				{
					if (screen == nullptr)
					{
						webos::log("[LIFECYCLE] pausing the game");
						pauseGame();
					}
					const auto nowTime = std::chrono::steady_clock::now();
					if (!level->isOnline && nowTime - lastBackgroundSave > std::chrono::seconds(10))
					{
						lastBackgroundSave = nowTime;
						level->forceSave(nullptr); // no progress screen: nothing may be drawn now
					}
				}
				const bool inBackground = webos::input::isBackgrounded();
				if (inBackground != bgAudioSuspended)
				{
					bgAudioSuspended = inBackground;
					soundEngine.setSuspended(inBackground);
				}
				if (inBackground)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
#endif

			if (pause && level != nullptr)
			{
				float aO = timer.a;
				timer.advanceTime();
				timer.a = aO;
			}
			else
			{
				timer.advanceTime();
			}

#ifdef MC_WEBOS
			g_perf.frameStart = std::chrono::steady_clock::now();
#endif
			// Tick game
			long_t tickNano = System::nanoTime();
			for (byte_t i = 0; i < timer.ticks; i++)
			{
				ticks++;
				fpsTicks++;
				try
				{
					tick();
				}
				catch (MinecraftException &)
				{
					// vanilla nulls theWorld before changeWorld1(null) so the
					// conflicting level is never saved over
					level = nullptr;
					setLevel(nullptr);
					setScreen(Util::make_shared<ConflictWarningScreen>(*this));
					break;
				}
			}
			long_t tickNanos = System::nanoTime() - tickNano;
#ifdef MC_WEBOS
			g_perf.mark = std::chrono::steady_clock::now();
			g_perf.tickMs += perfMs(g_perf.frameStart, g_perf.mark);
#endif

			// Update sound listener position every frame
			if (player != nullptr)
				soundEngine.update(player.get(), timer.a);

			checkGlError("Pre render");
#ifdef MC_WEBOS
			webos::stage("main loop: render");
#endif

			glEnable(GL_TEXTURE_2D);
			glEnable(GL_ALPHA_TEST);

			if (level != nullptr && !level->isOnline)
				level->updateLights();
			// Something was probably here at some point
			if (level != nullptr && level->isOnline)
				level->updateLights();


			// Render first, then present the completed frame.
			// The previous webOS port swapped here before rendering, which left
			// the freshly rendered title screen in the back buffer forever.
			if (!noRender)
			{
#ifdef MC_WEBOS
				webos::stage("render: game");
#endif
				if (gameMode != nullptr)
					gameMode->render(timer.a);
				gameRenderer.render(timer.a);
				if (achievementToast != nullptr)
					achievementToast->render();
			}

#ifdef MC_WEBOS
			{
				// Diagnostics: is anything actually being drawn, and does the
				// framebuffer contain it before it is presented?
				static unsigned long dbgFrame = 0;
				dbgFrame++;
				webos::stage("main loop: after render");
				// (the read-back diagnostic looks at the window framebuffer, not the offscreen target)
				if ((dbgFrame <= 3 || dbgFrame == 60 || dbgFrame % 1000 == 0) && gles2compat::renderScale() >= 0.999f)
				{
					unsigned long calls, noPos, noProg, drawn;
					gles2compat::stats(calls, noPos, noProg, drawn);
					unsigned char center[4] = {0, 0, 0, 0}, corner[4] = {0, 0, 0, 0};
					glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
					glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
					webos::log("[FRAME %lu] screen=%s level=%s noRender=%d size=%dx%d draws: calls=%lu noPos=%lu noProg=%lu drawn=%lu glError=0x%x center=(%d,%d,%d,%d) corner=(%d,%d,%d,%d)",
					           dbgFrame, screen != nullptr ? "yes" : "NULL", level != nullptr ? "yes" : "no", noRender ? 1 : 0, width, height,
					           calls, noPos, noProg, drawn, static_cast<unsigned>(glGetError()),
					           center[0], center[1], center[2], center[3], corner[0], corner[1], corner[2], corner[3]);
				}
			}
			// webOS Wayland may report no SDL input focus while the fullscreen
			// surface is still active. Never toggle fullscreen from that flag.
			// (no inactive-window sleep here: the flag is unreliable on the TV and a
			// 10 ms sleep would only cap the frame rate)
#else
			if (!lwjgl::Display::isActive())
			{
				if (fullscreen)
					toggleFullscreen();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
#endif
			if (options.showDebugInfo)
			{
				renderFpsMeter(tickNanos);
			}
			else
			{
				lastTimer = System::nanoTime();
			}

#ifdef MC_WEBOS
			webos::stage("main loop: swap");
			const auto perfRenderEnd = std::chrono::steady_clock::now();
			g_perf.renderMs += perfMs(g_perf.mark, perfRenderEnd);
#endif
			// Present the frame only after all world/GUI/title rendering is done.
			if (!lwjgl::Keyboard::isKeyDown(lwjgl::Keyboard::KEY_F7))
				lwjgl::Display::update();

#ifdef MC_WEBOS
			const auto perfSwapEnd = std::chrono::steady_clock::now();
			g_perf.swapMs += perfMs(perfRenderEnd, perfSwapEnd);
			g_perf.frameMs += perfMs(g_perf.frameStart, perfSwapEnd);
			g_perf.frames++;
#endif
			std::this_thread::yield();
			if (lwjgl::Keyboard::isKeyDown(lwjgl::Keyboard::KEY_F7))
				lwjgl::Display::update();

			screenshotListener();

			if (!fullscreen)
			{
				int_t w = lwjgl::Display::getWidth();
				int_t h = lwjgl::Display::getHeight();
				if (w != width || h != height)
				{
					width = w;
					height = h;
					if (width <= 0)
						width = 1;
					if (height <= 0)
						height = 1;
					resize(width, height);
				}
			}

			checkGlError("Post render");
#ifdef MC_WEBOS
			webos::frame();
#endif
			fpsFrames++;

			pause = !isOnline() && screen != nullptr && screen->isPauseScreen();

			while (System::currentTimeMillis() >= fpsMs + 1000)
			{
#ifdef MC_WEBOS
				if (g_perf.frames > 0)
				{
					unsigned long calls, noPos, noProg, drawn, verts, clientKb;
					gles2compat::stats(calls, noPos, noProg, drawn);
					gles2compat::bytesStats(verts, clientKb);
					static unsigned long lastDrawn = 0, lastVerts = 0, lastKb = 0;
					const double n = static_cast<double>(g_perf.frames);
					webos::log("[PERF] fps=%d frame=%.1fms (tick=%.1f render=%.1f swap=%.1f) draws/frame=%lu verts/frame=%lu clientKB/frame=%lu chunkUpdates=%d level=%s screen=%s scale=%.2f",
					           fpsFrames, g_perf.frameMs / n, g_perf.tickMs / n, g_perf.renderMs / n, g_perf.swapMs / n,
					           static_cast<unsigned long>((drawn - lastDrawn) / g_perf.frames),
					           static_cast<unsigned long>((verts - lastVerts) / g_perf.frames),
					           static_cast<unsigned long>((clientKb - lastKb) / g_perf.frames),
					           static_cast<int>(Chunk::updates), level != nullptr ? "yes" : "no", screen != nullptr ? "yes" : "no",
					           static_cast<double>(gles2compat::renderScale()));
					lastDrawn = drawn;
					lastVerts = verts;
					lastKb = clientKb;
					g_perf = PerfAccum();
				}
#endif
				fpsString = String::fromUTF8(std::to_string(fpsFrames) + " fps, " + std::to_string(Chunk::updates) + " chunk updates");
				Chunk::updates = 0;
				fpsMs += 1000;
				fpsFrames = 0;
			}
		}
#ifdef MC_WEBOS
		// Closed from the launcher / luna: write the world out on the way out.
		// (Silent, no progress screen; the exit failsafe ends the process if this
		// ever takes too long.)
		if (level != nullptr && !level->isOnline)
		{
			webos::log("[EXIT] saving world");
			level->forceSave(nullptr);
			webos::log("[EXIT] world saved");
		}
#endif
#ifdef NDEBUG
	}
	catch (std::exception &e)
	{
		onCrash("Unexpected error", e);
		return;
	}
	catch (...)
	{
		CrashHandler::Crash("Unexpected error: unknown exception");
		return;
	}
#endif
}

void Minecraft::renderFpsMeter(long_t tickNanos)
{
	// Update times
	long_t target = 16666666LL; // 1 / 60
	if (lastTimer == -1)
		lastTimer = System::nanoTime();

	long_t now = System::nanoTime();

	tickTimes[frameTimePos & (frameTimes.size() - 1)] = tickNanos;
	frameTimes[frameTimePos & (frameTimes.size() - 1)] = now - lastTimer;
	frameTimePos++;

	lastTimer = now;

	// Draw
	glClear(GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, width, height, 0.0, 1000.0, 3000.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0f, 0.0f, -2000.0f);

	glLineWidth(1.0f);
	glDisable(GL_TEXTURE_2D);

	Tesselator &t = Tesselator::instance;
	t.begin(GL_QUADS);

	int_t targetHeight = static_cast<int_t>(target / 200000LL);

	t.color(0x20000000);
	t.vertex(0.0, this->height - targetHeight, 0.0);
	t.vertex(0.0, this->height, 0.0);
	t.vertex(static_cast<double>(frameTimes.size()), this->height, 0.0);
	t.vertex(static_cast<double>(frameTimes.size()), this->height - targetHeight, 0.0);

	t.color(0x20200000);
	t.vertex(0.0, this->height - targetHeight * 2, 0.0);
	t.vertex(0.0, this->height - targetHeight, 0.0);
	t.vertex(static_cast<double>(frameTimes.size()), this->height - targetHeight, 0.0);
	t.vertex(static_cast<double>(frameTimes.size()), this->height - targetHeight * 2, 0.0);

	t.end();

	long_t total = 0;
	for (int_t i = 0; i < frameTimes.size(); i++)
		total += frameTimes[i];
	int_t avg = static_cast<int_t>(total / 200000LL / static_cast<long_t>(frameTimes.size()));

	t.begin(GL_QUADS);
	t.color(0x20400000);
	t.vertex(0.0, (height - avg), 0.0);
	t.vertex(0.0, height, 0.0);
	t.vertex(static_cast<double>(frameTimes.size()), height, 0.0);
	t.vertex(static_cast<double>(frameTimes.size()), (height - avg), 0.0);
	t.end();

	t.begin(GL_LINES);
	for (int_t i = 0; i < frameTimes.size(); i++)
	{
		int_t x = ((i - frameTimePos) & (static_cast<int_t>(frameTimes.size()) - 1)) * 255 / static_cast<int_t>(frameTimes.size());
		int_t m = x * x / 255;
		m = m * m / 255;
		int_t n = m * m / 255;
		n = n * n / 255;

		if (frameTimes[i] > target)
			t.color(0xFF000000 + (m * 0x010000));
		else
			t.color(0xFF000000 + (m * 0x0100));

		long_t ft = frameTimes[i] / 200000LL;
		long_t tt = tickTimes[i] / 200000LL;

		t.vertex((i + 0.5F), (static_cast<float>(height - ft) + 0.5F), 0.0);
		t.vertex((i + 0.5F), (height + 0.5F), 0.0);

		t.color(0xFF000000 + (m * 0x010000) + (m * 0x0100) + (m * 0x01));
		t.vertex((i + 0.5F), (static_cast<float>(height - ft) + 0.5F), 0.0);
		t.vertex((i + 0.5F), (static_cast<float>(height - ft - tt) + 0.5F), 0.0);
	}
	t.end();

	glEnable(GL_TEXTURE_2D);
}

void Minecraft::stop()
{
	running = false;
}

void Minecraft::grabMouse()
{
	if (unattended || !lwjgl::Display::isActive())
		return;
	if (mouseGrabbed)
		return;
	mouseGrabbed = true;
	mouseHandler.grab();
	setScreen(nullptr);
	lastClickTick = ticks + 10000;
}

void Minecraft::releaseMouse()
{
	if (!mouseGrabbed)
		return;
	mouseGrabbed = false;
	mouseHandler.release();
}

void Minecraft::pauseGame()
{
	if (this->screen != nullptr)
		return;
	setScreen(Util::make_shared<PauseScreen>(*this));
}

void Minecraft::handleMouseDown(int_t button, bool down)
{
	if (gameMode->instaBuild)
		return;
	if (button == 0 && missTime > 0)
		return;
	if (down && hitResult.type == HitResult::Type::TILE && button == 0)
	{
		int_t x = hitResult.x;
		int_t y = hitResult.y;
		int_t z = hitResult.z;
		gameMode->continueDestroyBlock(x, y, z, hitResult.f);
		particleEngine.crack(x, y, z, static_cast<int_t>(hitResult.f));
	}
	else
	{
		gameMode->stopDestroyBlock();
	}
}

void Minecraft::handleMouseClick(int_t button)
{
	if (button == 0 && missTime > 0)
		return;
	if (button == 0)
		player->swing();

	bool canUseItem = true;

	auto player = std::static_pointer_cast<Player>(this->player);

	if (hitResult.type == HitResult::Type::NONE)
	{
		if (button == 0 && !gameMode->isCreativeMode())
			missTime = 10;
	}
	else if (hitResult.type == HitResult::Type::ENTITY)
	{
		if (button == 0)
			gameMode->attack(player, hitResult.entity);
		else if (button == 1)
			gameMode->interact(player, hitResult.entity);
	}
	else if (hitResult.type == HitResult::Type::TILE)
	{
		int_t x = hitResult.x;
		int_t y = hitResult.y;
		int_t z = hitResult.z;
		Facing f = hitResult.f;

		if (button == 0)
		{
			level->extinguishFire(x, y, z, hitResult.f);
			Tile *targetTile = Tile::tiles[level->getTile(x, y, z)];
			if (targetTile != &Tile::bedrock || player->userType >= 100)
				gameMode->startDestroyBlock(x, y, z, hitResult.f);
		}
		else
		{
			ItemInstance *selected = player->getSelectedItem();
			int_t selectedCount = selected != nullptr
				? selected->stackSize.load(std::memory_order_relaxed) : 0;
			if (gameMode->useItemOn(player, *level, selected, x, y, z, f))
			{
				canUseItem = false;
				if (!player->sleeping)
					this->player->swing();
			}

			if (selected == nullptr)
				return;
			if (selected->isEmpty())
				return;
			if (selected->stackSize != selectedCount)
				gameRenderer.itemPlaced();
		}
	}

	if (canUseItem && button == 1)
	{
		ItemInstance *selected = player->getSelectedItem();
		if (selected != nullptr && gameMode->useItem(player, *level, selected))
			gameRenderer.itemUsed();
	}
}

void Minecraft::toggleFullscreen()
{
	fullscreen = !fullscreen;
	std::cout << "Toggle fullscreen!\n";

	lwjgl::Display::setFullscreen(fullscreen);
	width = lwjgl::Display::getWidth();
	height = lwjgl::Display::getHeight();
	if (width <= 0)
		width = 1;
	if (height <= 0)
		height = 1;

	releaseMouse();
	lwjgl::Display::update();

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	if (fullscreen)
		grabMouse();

	if (screen != nullptr)
	{
		releaseMouse();
		resize(width, height);
	}

	std::cout << "Size: " << width << ", " << height << '\n';
}

void Minecraft::resize(int_t w, int_t h)
{
	if (w <= 0)
		w = 1;
	if (h <= 0)
		h = 1;

	width = w;
	height = h;

	if (screen != nullptr)
	{
		ScreenSizeCalculator ssc(options, width, height);
		int_t w = ssc.getWidth();
		int_t h = ssc.getHeight();
		screen->init(w, h);
	}
}

// B173-JAVA-METHOD: net.minecraft.client.Minecraft#screenshotListener()
void Minecraft::screenshotListener()
{
	if (lwjgl::Keyboard::isKeyDown(lwjgl::Keyboard::KEY_F2))
	{
		if (!isTakingScreenshot)
		{
			isTakingScreenshot = true;
			SPCCommand::addChatMessage(ScreenShotHelper::saveScreenshot(*getWorkingDirectory(), width, height));
		}
	}
	else
	{
		isTakingScreenshot = false;
	}
}

void Minecraft::handleGrabTexture()
{
	if (hitResult.type != HitResult::Type::NONE)
	{
		int_t i = level->getTile(hitResult.x, hitResult.y, hitResult.z);
		if (i == Tile::grass.id)
			i = Tile::dirt.id;
		if (i == Tile::slabDouble.id)
			i = Tile::slabSingle.id;
		if (i == Tile::bedrock.id)
			i = Tile::rock.id;
		player->inventory.setCurrentItem(i);
	}
}

void Minecraft::tick()
{
	Profiler::Scope tickProfile(Profiler::Section::Tick);
	gameRenderer.pick(1.0f);

	if (player != nullptr)
	{
		player->prepareForTick();

	}

	// 
	if (!pause && level != nullptr)
		gameMode->tick();

	// Texture update
	glBindTexture(GL_TEXTURE_2D, textures.loadTexture(u"/terrain.png"));
	if (!pause)
		textures.tick();

	if (!pause)
	{
		soundEngine.updateOptions();
		soundEngine.playMusicTick();
	}

	if (screen == nullptr && player != nullptr)
	{
		if (player->health <= 0)
			setScreen(nullptr);
		else if (player->isPlayerSleeping() && level != nullptr && level->isOnline)
			setScreen(Util::make_shared<SleepScreen>(*this));
	}
	else if (screen != nullptr && dynamic_cast<SleepScreen *>(screen.get()) != nullptr
		&& player != nullptr && !player->isPlayerSleeping())
	{
		setScreen(nullptr);
	}

	// Tick screen
	if (screen != nullptr)
		lastClickTick = ticks + 10000;
	if (screen != nullptr)
	{
		std::shared_ptr<Screen> eventScreen = screen;
		eventScreen->updateEvents();
		if (screen != nullptr)
		{
			std::shared_ptr<Screen> tickScreen = screen;
			tickScreen->tick();
		}
	}

	// Event processing
	if (!unattended && (screen == nullptr || screen->passEvents))
	{
		while (lwjgl::Mouse::next())
		{
			long_t tickDelta = System::currentTimeMillis() - lastTickTime;
			if (tickDelta > 200)
				continue;

			int_t dwheel = lwjgl::Mouse::getEventDWheel();
			if (dwheel != 0 && screen == nullptr && player != nullptr)
				player->inventory.changeCurrentItem(dwheel);

			if (screen == nullptr)
			{
				if (!mouseGrabbed && lwjgl::Mouse::getEventButtonState())
				{
					grabMouse();
					continue;
				}
				if (lwjgl::Mouse::getEventButton() == 0 && lwjgl::Mouse::getEventButtonState())
				{
					handleMouseClick(0);
					lastClickTick = ticks;
				}
				if (lwjgl::Mouse::getEventButton() == 1 && lwjgl::Mouse::getEventButtonState())
				{
					handleMouseClick(1);
					lastClickTick = ticks;
				}
				if (lwjgl::Mouse::getEventButton() == 2 && lwjgl::Mouse::getEventButtonState())
					handleGrabTexture();
				continue;
			}
			if (screen != nullptr)
			{
				std::shared_ptr<Screen> activeScreen = screen;
				activeScreen->mouseEvent();
			}
		}

		if (missTime > 0)
			missTime--;

		while (lwjgl::Keyboard::next())
		{
			if (player != nullptr)
				player->setKey(lwjgl::Keyboard::getEventKey(), lwjgl::Keyboard::getEventKeyState());

			if (lwjgl::Keyboard::getEventKeyState())
			{
				if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_F11)
				{
					toggleFullscreen();
					continue;
				}

				if (screen != nullptr)
				{
					std::shared_ptr<Screen> activeScreen = screen;
					activeScreen->keyboardEvent();
				}
				else
				{
					if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_ESCAPE)
						pauseGame();
					if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_F1)
						options.hideGui = !options.hideGui;
					if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_F3)
						options.showDebugInfo = !options.showDebugInfo;
					if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_F5)
						options.thirdPersonView = !options.thirdPersonView;
					if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_F8)
						options.smoothCamera = !options.smoothCamera;
					if (lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_F7 && player != nullptr)
						setScreen(Util::make_shared<DebugMenuScreen>(*this));
					if (lwjgl::Keyboard::getEventKey() == options.keyDrop.key && player != nullptr)
						player->drop();
					if (lwjgl::Keyboard::getEventKey() == options.keyInventory.key && player != nullptr)
						setScreen(Util::make_shared<InventoryScreen>(*this));
					if (lwjgl::Keyboard::getEventKey() == options.keyChat.key && player != nullptr)
						setScreen(Util::make_shared<ChatScreen>(*this));
				}

				for (int_t i = 0; i < 9; i++)
				{
					if (player != nullptr && lwjgl::Keyboard::getEventKey() == lwjgl::Keyboard::KEY_1 + i)
						player->inventory.currentItem = i;
				}

				if (lwjgl::Keyboard::getEventKey() == options.keyFog.key)
					options.toggle(Options::Option::RENDER_DISTANCE, (lwjgl::Keyboard::isKeyDown(lwjgl::Keyboard::KEY_LSHIFT) || lwjgl::Keyboard::isKeyDown(lwjgl::Keyboard::KEY_RSHIFT)) ? -1 : 1);
			}
		}

		if (screen == nullptr)
		{
			if (lwjgl::Mouse::isButtonDown(0) && static_cast<float>(ticks - lastClickTick) >= (timer.ticksPerSecond / 4.0f) && mouseGrabbed)
			{
				handleMouseClick(0);
				lastClickTick = ticks;
			}
			if (lwjgl::Mouse::isButtonDown(1) && static_cast<float>(ticks - lastClickTick) >= (timer.ticksPerSecond / 4.0f) && mouseGrabbed)
			{
				handleMouseClick(1);
				lastClickTick = ticks;
			}
		}

		handleMouseDown(0, (screen == nullptr && lwjgl::Mouse::isButtonDown(0) && mouseGrabbed));
	}

	// Level tick
	if (level != nullptr)
	{
		if (player != nullptr)
		{
			recheckPlayerIn++;
			if (recheckPlayerIn == 30)
			{
				recheckPlayerIn = 0;
				level->ensureAdded(player);
			}
		}

		level->difficulty = options.difficulty;
		if (level->isOnline)
			level->difficulty = 3;

		if (!pause)
			gameRenderer.tick();
		if (!pause)
			levelRenderer.tick();
		if (!pause)
		{
			if (level->lastLightningBolt > 0)
				--level->lastLightningBolt;
			level->tickEntities();
		}
		if (!pause || isOnline())
		{
			level->setSpawnSettings(!unattended && options.difficulty > 0, !unattended);
			level->tick();
		}
		if (!pause && level != nullptr)
			level->animateTick(Mth::floor(player->x), Mth::floor(player->y), Mth::floor(player->z));
		if (!pause)
			particleEngine.tick();
		if (statFileWriter != nullptr)
			statFileWriter->tick();
		if (!pause)
			gui.tick();
	}

	lastTickTime = System::currentTimeMillis();
}

void Minecraft::stressTick()
{
	++ticks;
	tick();
}

bool Minecraft::isOnline()
{
	return level != nullptr && level->isOnline;
}

void Minecraft::selectLevel(const jstring &name)
{
	selectLevel(name, name, Random().nextLong());
}

void Minecraft::selectLevel(const jstring &name, const jstring &levelName, long_t seed)
{
	setLevel(nullptr);

	std::unique_ptr<File> saves(File::open(*getWorkingDirectory(), u"saves"));
	SaveConverterMcRegion converter(*saves);
	if (converter.isOldMapFormat(name))
	{
		progressRenderer->progressStart(u"Converting World to " + converter.getFormatName());
		progressRenderer->progressStage(u"This may take a while :)");
		converter.convertMapFormat(name, *progressRenderer);
		selectLevel(name, levelName, 0LL);
		return;
	}
	std::shared_ptr<Level> new_level = Util::make_shared<Level>(saves.release(), name, levelName, seed);
	if (statFileWriter != nullptr)
	{
		if (new_level->isNew && StatList::createWorldStat != nullptr)
			statFileWriter->readStat(*StatList::createWorldStat, 1);
		else if (!new_level->isNew && StatList::loadWorldStat != nullptr)
			statFileWriter->readStat(*StatList::loadWorldStat, 1);
		if (StatList::startGameStat != nullptr)
			statFileWriter->readStat(*StatList::startGameStat, 1);
	}
	if (new_level->isNew)
		setLevel(new_level, u"Generating level");
	else
		setLevel(new_level, u"Loading level");
}

// Minecraft.usePortal
void Minecraft::toggleDimension()
{
	if (level == nullptr || player == nullptr)
		return;

	std::cout << "Toggling dimension!!" << '\n';
	int_t newDimension = (player->dimension == -1) ? 0 : -1;
	player->dimension = newDimension;

	level->removeEntity(player);
	player->removed = false;

	double x = player->x;
	double z = player->z;
	double scale = 8.0;
	if (newDimension == -1)
	{
		x /= scale;
		z /= scale;
	}
	else
	{
		x *= scale;
		z *= scale;
	}
	player->moveTo(x, player->y, z, player->yRot, player->xRot);
	// vanilla updateEntityWithOptionalForce(player, false): position/chunk
	// bookkeeping only, never onUpdate (which would re-fire the portal timer)
	if (player->isAlive())
		level->tick(player, false);

	// vanilla carries the same player object across; Entity holds a Level
	// reference, so the port recreates the player in the new level and moves
	// its state over via NBT (same technique the respawn path uses)
	CompoundTag playerTag;
	player->saveWithoutId(playerTag);

	std::shared_ptr<Level> newLevel = Util::make_shared<Level>(*level, newDimension);
	std::shared_ptr<LocalPlayer> newPlayer = std::static_pointer_cast<LocalPlayer>(gameMode->createPlayer(*newLevel));
	newPlayer->load(playerTag);
	newPlayer->dimension = newDimension;
	newPlayer->moveTo(x, player->y, z, player->yRot, player->xRot);
	this->player = newPlayer;

	setLevel(newLevel, newDimension == -1 ? u"Entering the Nether" : u"Leaving the Nether", newPlayer);

	if (newPlayer->isAlive())
	{
		newPlayer->moveTo(x, newPlayer->y, z, newPlayer->yRot, newPlayer->xRot);
		newLevel->tick(newPlayer, false);
		Teleporter().teleport(*newLevel, *newPlayer);
	}
}

void Minecraft::setLevel(std::shared_ptr<Level> level)
{
	setLevel(level, u"");
}

void Minecraft::setLevel(std::shared_ptr<Level> level, const jstring &title)
{
	setLevel(level, title, nullptr);
}

void Minecraft::setLevel(std::shared_ptr<Level> level, const jstring &title, std::shared_ptr<Player> player)
{
	if (statFileWriter != nullptr)
		statFileWriter->syncStats();
	progressRenderer->progressStart(title);
	progressRenderer->progressStage(u"");

	if (this->level != nullptr)
		this->level->forceSave(progressRenderer);
	this->level = level;

	std::cout << "Player is " << this->player << '\n';

	if (level != nullptr)
	{
		gameMode->initLevel(level);

		if (!isOnline())
		{
			if (player == nullptr)
				this->player = nullptr; // level->findSubclassOf<LocalPlayer>();
		}
		else if (this->player != nullptr)
		{
			this->player->resetPos();
			if (level != nullptr)
				level->addEntity(this->player);
		}

		if (!level->isOnline)
			prepareLevel(title);

		std::cout << "Player is now " << this->player << '\n';

		if (this->player == nullptr)
		{
			this->player = std::static_pointer_cast<LocalPlayer>(gameMode->createPlayer(*level));
			this->player->resetPos();
			gameMode->initPlayer(this->player);
		}
		this->player->input = std::make_unique<KeyboardInput>(options);

		levelRenderer.setLevel(level);

		particleEngine.setLevel(level.get());

		gameMode->adjustPlayer(this->player);
		if (player != nullptr)
			level->clearLoadedPlayerData();


		level->loadPlayer(this->player);

		if (level->isNew)
			level->forceSave(progressRenderer);
	}
	else
	{
		this->player = nullptr;
	}
}

void Minecraft::prepareLevel(const jstring &title)
{
	progressRenderer->progressStart(title);
	progressRenderer->progressStage(u"Building terrain");

	short_t radius = 128;
	int_t i = 0;
	int_t max = radius * 2 / 16 + 1;
	max *= max;

	int_t xSpawn = level->xSpawn;
	int_t zSpawn = level->zSpawn;
	if (player != nullptr)
	{
		xSpawn = static_cast<int_t>(player->x);
		zSpawn = static_cast<int_t>(player->z);
	}


	for (int_t x = -radius; x <= radius; x += 16)
	{
		for (int_t z = -radius; z <= radius; z += 16)
		{
			progressRenderer->progressStagePercentage(i++ * 100 / max);
			level->getTile(xSpawn + x, 64, zSpawn + z);

			while (level->updateLights());
		}
	}

	progressRenderer->progressStage(u"Simulating world for a bit");
}

jstring Minecraft::gatherStats1()
{
	return levelRenderer.gatherStats1();
}

jstring Minecraft::gatherStats2()
{
	return levelRenderer.gatherStats2();
}

jstring Minecraft::gatherStats4()
{
	return level->gatherChunkSourceStats();
}

jstring Minecraft::gatherStats3()
{
	return u"P: " + particleEngine.countParticles() + u". T: " + level->gatherStats();
}

void Minecraft::respawnPlayer(int_t dimension)
{
	if (level == nullptr || gameMode == nullptr)
		return;

	if (!level->isOnline && level->dimension != nullptr && !level->dimension->mayRespawn())
		toggleDimension();

	level->setSpawnLocation();
	level->updateEntityList();

	int_t entityId = 0;
	if (player != nullptr)
	{
		entityId = player->entityId;
		level->removeEntity(player);
	}

	this->player = std::static_pointer_cast<LocalPlayer>(gameMode->createPlayer(*level));
	this->player->dimension = dimension;
	this->player->resetPos();
	gameMode->initPlayer(this->player);
	level->loadPlayer(this->player);
	this->player->input = std::make_unique<KeyboardInput>(options);
	this->player->entityId = entityId;
	this->player->animateRespawn();
	gameMode->adjustPlayer(this->player);
	prepareLevel(u"Respawning");

	if (std::dynamic_pointer_cast<DeathScreen>(screen) != nullptr)
		setScreen(nullptr);
}

void Minecraft::start(const jstring *name, const jstring *sessionId)
{
	startAndConnectTo(name, sessionId, nullptr);
}

void Minecraft::startAndConnectTo(const jstring *name, const jstring *sessionId, const jstring *ip)
{
#ifdef MC_WEBOS
	// The TV window is always full screen (the size comes from the display).
	std::unique_ptr<Minecraft> minecraft = std::make_unique<Minecraft>(1920, 1080, true);
#else
	std::unique_ptr<Minecraft> minecraft = std::make_unique<Minecraft>(854, 480, false);
#endif

	minecraft->serverDomain = u"www.minecraft.net";
	if (name != nullptr && sessionId != nullptr)
		minecraft->user = std::make_unique<User>(*name, *sessionId);
	else
		minecraft->user = std::make_unique<User>(u"Player" + String::fromUTF8(std::to_string(System::currentTimeMillis() % 1000)), u"");
	
	if (ip != nullptr)
	{
		std::string address = String::toUTF8(*ip);
		size_t separator = address.rfind(':');
		if (separator == std::string::npos)
			throw std::invalid_argument("Server address must be host:port");
		int port = std::stoi(address.substr(separator + 1));
		if (port < 0 || port > 65535)
			throw std::out_of_range("Server port is out of range");
		minecraft->serverHost = address.substr(0, separator);
		minecraft->serverPort = static_cast<std::uint16_t>(port);
	}

	minecraft->run();
	// minecraft.release(); // TODO: fix destructor order stuff
}

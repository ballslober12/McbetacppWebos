#include "lwjgl/GLContext.h"

#include <iostream>
#include <stdexcept>
#include <csignal>
#include <cstdlib>

#include "external/SDLException.h"

#include "SDL.h"

#ifdef MC_WEBOS
#include "webos/GLES2Compat.h"
#include "webos/WebOSLog.h"
#endif

// #define MC_DEBUG_GL

#ifdef MC_DEBUG_GL
static void GLDebugMessageCallback(GLenum source, GLenum type, GLuint id,
                            GLenum severity, GLsizei length,
                            const GLchar *msg, const void *data)
{
    const char* _source;
    const char* _type;
    const char* _severity;

    switch (source) {
        case GL_DEBUG_SOURCE_API:
        _source = "API";
        break;

        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        _source = "WINDOW SYSTEM";
        break;

        case GL_DEBUG_SOURCE_SHADER_COMPILER:
        _source = "SHADER COMPILER";
        break;

        case GL_DEBUG_SOURCE_THIRD_PARTY:
        _source = "THIRD PARTY";
        break;

        case GL_DEBUG_SOURCE_APPLICATION:
        _source = "APPLICATION";
        break;

        case GL_DEBUG_SOURCE_OTHER:
        _source = "UNKNOWN";
        break;

        default:
        _source = "UNKNOWN";
        break;
    }

    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
        _type = "ERROR";
        break;

        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        _type = "DEPRECATED BEHAVIOR";
        break;

        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        _type = "UDEFINED BEHAVIOR";
        break;

        case GL_DEBUG_TYPE_PORTABILITY:
        _type = "PORTABILITY";
        break;

        case GL_DEBUG_TYPE_PERFORMANCE:
        _type = "PERFORMANCE";
        break;

        case GL_DEBUG_TYPE_OTHER:
        _type = "OTHER";
        break;

        case GL_DEBUG_TYPE_MARKER:
        _type = "MARKER";
        break;

        default:
        _type = "UNKNOWN";
        break;
    }

    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
        _severity = "HIGH";
        break;

        case GL_DEBUG_SEVERITY_MEDIUM:
        _severity = "MEDIUM";
        break;

        case GL_DEBUG_SEVERITY_LOW:
        _severity = "LOW";
        break;

        case GL_DEBUG_SEVERITY_NOTIFICATION:
        _severity = "NOTIFICATION";
        break;

        default:
        _severity = "UNKNOWN";
        break;
    }

    printf("%d: %s of %s severity, raised from %s: %s\n",
            id, _type, _severity, _source, msg);
	std::raise(SIGINT);
}
#endif

namespace lwjgl
{
namespace GLContext
{

// Detail implementation
namespace detail
{

// Context singleton
class GLContext
{
private:
	SDL_Window *window = nullptr;
	SDL_GLContext gl_context = nullptr;
	GLCapabilities capabilties;

public:
	GLContext()
	{
#ifdef MC_WEBOS
		createWebOSContext();
#else
		// B173 - Fixed-function rendering uses the OpenGL 2.1 compatibility API.
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

#ifdef MC_DEBUG_GL
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

		// Create SDL window
		window = SDL_CreateWindow("McBetaCpp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 854, 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
		if (window == nullptr)
			throw SDLException();

		// Create OpenGL context
		gl_context = SDL_GL_CreateContext(window);
		if (gl_context == nullptr)
			throw SDLException();

		if (SDL_GL_MakeCurrent(window, gl_context))
			throw SDLException();

		// Load GLAD
		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
			throw std::runtime_error("Failed to load glad");
#endif

#ifdef MC_WEBOS
		// The TV compositor paces frames; an unthrottled loop only burns CPU. Ignore
		// failures, some webOS backends do not implement swap intervals.
		// MCBETA_SWAP_INTERVAL=0 turns vsync off if a frame callback never arrives.
		int swapInterval = 1;
		if (const char *env = std::getenv("MCBETA_SWAP_INTERVAL"))
			swapInterval = std::atoi(env);
		if (SDL_GL_SetSwapInterval(swapInterval) != 0)
			webos::log("[GL] SDL_GL_SetSwapInterval(%d) failed: %s", swapInterval, SDL_GetError());
#else
		// Disable VSync
		SDL_GL_SetSwapInterval(0);
#endif

		// Parse capabilities
		{
			const GLubyte *extensions = glGetString(GL_EXTENSIONS);
			std::string cap;

			const char *extension_p = extensions != nullptr ? reinterpret_cast<const char *>(extensions) : "";
			while (*extension_p != '\0')
			{
				if (*extension_p == ' ')
				{
					if (!cap.empty())
					{
						capabilties.add(cap);
						cap.clear();
					}
					while (*extension_p == ' ')
						extension_p++;
					continue;
				}

				cap.push_back(*extension_p++);
			}
		}

#ifdef MC_DEBUG_GL
		// Enable debugging
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(GLDebugMessageCallback, nullptr);
#endif
	}

#ifdef MC_WEBOS
	// OpenGL ES 2.0 window/context for the TV. Beta 1.7.3 draws with the
	// fixed-function pipeline, so gles2compat provides those calls on ES 2.0.
	void createWebOSContext()
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		// The working 3dsimple demo (SDL_Renderer) never asks for destination
		// alpha. A Wayland surface WITH alpha is composited as translucent, so any
		// pixel the game writes with alpha 0 (its clear colour is 0,0,0,0) shows
		// the black background instead of the game. Default to an opaque buffer;
		// MCBETA_ALPHA=1 restores the old 8-bit alpha channel.
		const char *alphaEnv = std::getenv("MCBETA_ALPHA");
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, (alphaEnv != nullptr && *alphaEnv == '1') ? 8 : 0);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		// Fill the screen: the app is shown full screen by the compositor anyway.
		int width = 1920, height = 1080;
		SDL_DisplayMode desktop;
		if (SDL_GetDesktopDisplayMode(0, &desktop) == 0 && desktop.w > 0 && desktop.h > 0)
		{
			width = desktop.w;
			height = desktop.h;
		}

		// Ask for a 24-bit depth buffer first; some ES drivers only have 16.
		const int depthSizes[] = {24, 16};
		for (int depth : depthSizes)
		{
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depth);
			// The working 3dsimple demo opens a plain SHOWN window (no fullscreen
			// flag) and the webOS compositor presents it full screen. Asking SDL
			// for FULLSCREEN_DESKTOP is the main difference from that demo, so it
			// is now opt-in: MCBETA_FULLSCREEN=1.
			const char *fsEnv = std::getenv("MCBETA_FULLSCREEN");
			Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;
			if (fsEnv != nullptr && *fsEnv == '1')
				windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
			window = SDL_CreateWindow("McBetaCpp", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, windowFlags);
			if (window == nullptr)
			{
				webos::log("[GL] SDL_CreateWindow (depth %d) failed: %s", depth, SDL_GetError());
				continue;
			}
			gl_context = SDL_GL_CreateContext(window);
			if (gl_context != nullptr)
			{
				int actualW = 0, actualH = 0, drawW = 0, drawH = 0, alphaBits = -1;
				SDL_GetWindowSize(window, &actualW, &actualH);
				SDL_GL_GetDrawableSize(window, &drawW, &drawH);
				SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &alphaBits);
				webos::log("[GL] window requested %dx%d, actual %dx%d, drawable %dx%d, %d-bit depth, %d alpha bits, flags=0x%x",
				           width, height, actualW, actualH, drawW, drawH, depth, alphaBits, SDL_GetWindowFlags(window));
				break;
			}
			webos::log("[GL] SDL_GL_CreateContext (depth %d) failed: %s", depth, SDL_GetError());
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		if (window == nullptr || gl_context == nullptr)
			throw SDLException();

		if (SDL_GL_MakeCurrent(window, gl_context))
			throw SDLException();

		if (!gles2compat::init(reinterpret_cast<gles2compat::LoaderFn>(SDL_GL_GetProcAddress)))
			throw std::runtime_error("Failed to initialise the OpenGL ES 2.0 compatibility layer");
	}
#endif

	SDL_Window *getWindow() const { return window; }
	SDL_GLContext getGLContext() const { return gl_context; }
	const GLCapabilities &getCapabilities() const { return capabilties; }
};

// Context singletons
static GLContext &getContext()
{
	static GLContext context;
	return context;
}

SDL_Window *getWindow()
{
	return getContext().getWindow();
}
SDL_GLContext getGLContext()
{
	return getContext().getGLContext();
}

}

// GL capabilities
void instantiate()
{
	detail::getContext();
	if (SDL_GL_MakeCurrent(detail::getContext().getWindow(), detail::getContext().getGLContext()))
		throw SDLException();
}

const detail::GLCapabilities &getCapabilities()
{
	return detail::getContext().getCapabilities();
}

}
}

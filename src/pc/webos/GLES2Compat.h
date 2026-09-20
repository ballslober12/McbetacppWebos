#pragma once
// OpenGL ES 2.0 back end for the game's fixed-function OpenGL calls.
//
// Beta 1.7.3 renders with the OpenGL 1.x fixed-function pipeline (matrix
// stacks, lighting, fog, alpha test, display lists, client-state arrays and
// GL_QUADS). The webOS TV only offers an OpenGL ES 2.0 context through SDL2,
// so this layer re-implements those calls on top of ES 2.0 shaders.
//
// The game code is not modified for this: game sources keep calling glEnable,
// glTranslatef, glCallList... which resolve to the glad function pointers, and
// init() below points those glad pointers at this layer instead of loading a
// desktop driver.
#include <glad/glad.h>

namespace gles2compat
{
typedef void *(*LoaderFn)(const char *name);

// Loads the real ES 2.0 entry points through `loader` (SDL_GL_GetProcAddress)
// and redirects the glad pointers used by the game. Must be called once with
// an ES 2.0 context current. Returns false when a required ES 2.0 function is
// missing.
bool init(LoaderFn loader);

// Draws an arrow-shaped mouse cursor on top of the current frame. (x, y) is the
// hot spot in pixels with the origin at the top left of a screenWidth x
// screenHeight framebuffer; `scale` multiplies the 20 pixel high arrow. All GL
// state that is touched is restored, so it can be called right before the
// buffer swap.
void drawCursor(float x, float y, int screenWidth, int screenHeight, float scale);

// Sets the alpha channel of the whole back buffer to 1 (colour untouched).
// Call right before swapping buffers; the TV compositor treats alpha < 1 as
// transparent.
void makeOpaque();

// Draw-call counters for diagnostics: glDrawArrays calls seen, dropped because
// no vertex array was enabled, dropped because the shader program was unusable,
// and actually submitted to the driver.
void stats(unsigned long &calls, unsigned long &noPos, unsigned long &noProg, unsigned long &drawn);

// Releases the shader programs, display lists and helper state.
void shutdown();
}

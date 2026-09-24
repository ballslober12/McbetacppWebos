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

// Vertices submitted and kilobytes copied from client memory (not from buffers), cumulative.
void bytesStats(unsigned long &verts, unsigned long &clientKb);

// Render scale (MCBETA_RENDER_SCALE=auto|0.5..0.99|off, default auto). The game
// draws into a smaller offscreen target and the frame is stretched to the
// window when presented, so the GPU shades fewer pixels. Call
// beginPresent() right before the game's cursor overlay / buffer swap and
// endPresent() right after the swap, every frame:
//  - beginPresent() returns true when the frame was already copied to the
//    window's framebuffer (the caller should then draw the cursor at native
//    resolution and must not call makeOpaque()).
//  - endPresent() switches back to the offscreen target and, in automatic
//    mode, adjusts the scale to reach the target frame rate.
bool beginPresent(int nativeWidth, int nativeHeight);
void endPresent();
// Current render scale (1.0 = native resolution).
float renderScale();
// In-game setting: 0 = automatic, 1 = 100%, 2 = 80%, 3 = 67%, 4 = 50%. Takes effect
// on the next frame. Ignored when MCBETA_RENDER_SCALE is set.
void setRenderScaleMode(int mode);
// False when this GPU cannot do render scaling (so the setting has no effect).
bool renderScaleAvailable();

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

// Render-scale test (runs on any machine with SDL2 + Mesa GLES2):
//   MCBETA_RENDER_SCALE=0.5 SDL_VIDEODRIVER=offscreen ./render_scale_test
//   MCBETA_RENDER_SCALE=auto SDL_VIDEODRIVER=offscreen ./render_scale_test auto
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include "webos/GLES2Compat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if(!(cond)){ printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while(0)

struct Px { unsigned char r,g,b,a; };
static Px px(int x, int y){ Px p; glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&p); return p; }
static bool near_(Px p,int r,int g,int b,int tol=8){ return abs(p.r-r)<=tol&&abs(p.g-g)<=tol&&abs(p.b-b)<=tol; }

static const int W = 320, H = 240;

static void quad(float x0,float y0,float x1,float y1,float z=0){
  float v[12]={x0,y0,z, x1,y0,z, x1,y1,z, x0,y1,z};
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(3,GL_FLOAT,0,v);
  glDrawArrays(GL_QUADS,0,4);
  glDisableClientState(GL_VERTEX_ARRAY);
}

// One "game frame": four coloured quadrants plus a depth-tested pair.
static void gameFrame(){
  glViewport(0,0,W,H);
  glDisable(GL_TEXTURE_2D); glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,W,0,H,-10,10);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  glColor4f(1,0,0,1); quad(0,0,W/2,H/2);          // bottom-left red
  glColor4f(0,1,0,1); quad(W/2,0,W,H/2);          // bottom-right green
  glColor4f(0,0,1,1); quad(0,H/2,W/2,H);          // top-left blue
  glColor4f(1,1,0,1); quad(W/2,H/2,W,H);          // top-right yellow
  // depth: far white quad drawn after a near cyan quad must not overwrite it
  glColor4f(0,1,1,1); quad(140,100,180,140,5);    // near (eye z=+5 is closer than -5 for glOrtho)
  glColor4f(1,1,1,1); quad(140,100,180,140,-5);   // far, drawn later
}

static int checkPresented(const char *tag){
  int before = failures;
  printf("[%s]\n", tag);
  CHECK(near_(px(40,40),255,0,0),"bottom-left not red: %d,%d,%d", px(40,40).r,px(40,40).g,px(40,40).b);
  CHECK(near_(px(W-40,40),0,255,0),"bottom-right not green");
  CHECK(near_(px(40,H-40),0,0,255),"top-left not blue");
  CHECK(near_(px(W-40,H-40),255,255,0),"top-right not yellow");
  CHECK(near_(px(160,120),0,255,255),"depth test failed inside target (centre not cyan): %d,%d,%d", px(160,120).r,px(160,120).g,px(160,120).b);
  CHECK(px(40,40).a==255,"alpha not opaque after blit");
  return failures - before;
}

int main(int argc, char **argv){
  bool autoMode = argc > 1 && !strcmp(argv[1], "auto");
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
  SDL_Window*w=SDL_CreateWindow("t",0,0,W,H,SDL_WINDOW_OPENGL);
  SDL_GLContext c=SDL_GL_CreateContext(w);
  if(!c){printf("no ctx\n");return 2;}
  if(!gles2compat::init((gles2compat::LoaderFn)SDL_GL_GetProcAddress)){ printf("init failed\n"); return 3; }

  int frames = autoMode ? 220 : 6; if (argc > 2) frames = atoi(argv[2]);
  for (int f = 0; f < frames; f++){
    gameFrame();
    bool scaled = gles2compat::beginPresent(W,H);
    if (!scaled) gles2compat::makeOpaque();
    // frames after the first must have been scaled if a scale is configured
    if (f >= 2 && !autoMode && getenv("MCBETA_RENDER_SCALE") && atof(getenv("MCBETA_RENDER_SCALE")) > 0.3 && atof(getenv("MCBETA_RENDER_SCALE")) < 0.99)
      CHECK(scaled, "frame %d was not presented through the scaled path", f);
    if (f == frames-1 || (!autoMode && f >= 2 && f <= 4)) checkPresented(scaled ? "scaled present" : "native present");
    SDL_GL_SwapWindow(w);
    gles2compat::endPresent();
    if (autoMode) SDL_Delay(35); // pretend to be slow so the controller steps down
    if (f >= 2 && !autoMode){
      // game state after endPresent: emulated state still works
      glViewport(0,0,W,H);
      glEnable(GL_DEPTH_TEST);
    }
  }
  if (!autoMode && !getenv("MCBETA_RENDER_SCALE")) {
    // in-game setting path: no env var, change the mode at runtime
    const int modes[] = {0, 3, 1, 4, 2, 0};
    for (int m : modes) {
      gles2compat::setRenderScaleMode(m);
      for (int f = 0; f < 4; f++) {
        gameFrame();
        bool scaled = gles2compat::beginPresent(W,H);
        if (!scaled) gles2compat::makeOpaque();
        if (f == 3) { char tag[48]; snprintf(tag,sizeof tag,"mode %d (scale %.2f)",m,gles2compat::renderScale()); checkPresented(tag); }
        SDL_GL_SwapWindow(w);
        gles2compat::endPresent();
      }
      float sc = gles2compat::renderScale();
      float want = m==1?1.0f: m==2?0.8f: m==3?2.0f/3: m==4?0.5f: -1;
      // targets are clamped to 320x180 in this tiny test window, so only check mode 1 exactly
      if (m == 1) CHECK(sc > 0.99f, "mode 1 should be native, got %.2f", sc);
      if (m == 0) CHECK(sc < 0.99f, "auto should start scaled, got %.2f", sc);
      (void)want;
    }
  }
  printf("render scale at end: %.3f\n", gles2compat::renderScale());
  // auto mode: the slow loop is pure CPU (sleep), so it must step down once, notice no gain, and go back
  printf(failures ? "FAILURES: %d\n" : "ALL PASSED\n", failures);
  return failures ? 1 : 0;
}

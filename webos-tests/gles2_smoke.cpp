#include <SDL2/SDL.h>
#include <glad/glad.h>
#include "webos/GLES2Compat.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

static int failures = 0;
#define CHECK(cond, ...) do { if(!(cond)){ printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while(0)

struct Px { unsigned char r,g,b,a; };
static Px px(int x, int y){ Px p; glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&p); return p; }
static bool near(Px p,int r,int g,int b,int tol=6){ return abs(p.r-r)<=tol&&abs(p.g-g)<=tol&&abs(p.b-b)<=tol; }
static void dump(const char*n,Px p){ printf("  %s = (%d,%d,%d,%d)\n",n,p.r,p.g,p.b,p.a); }

static bool glIsEnabledProbe(){
  // draw a red quad with blending disabled and depth test on: must fully overwrite
  glColor4f(1,0,0,0.5f); float v[12]={150,150,0.5f,250,150,0.5f,250,250,0.5f,150,250,0.5f};
  glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3,GL_FLOAT,0,v); glDrawArrays(GL_QUADS,0,4); glDisableClientState(GL_VERTEX_ARRAY);
  Px p=px(200,200); printf("  post-cursor game draw = (%d,%d,%d)\n",p.r,p.g,p.b); return near(p,255,0,0);
}
static void reset2D(){
  glDisable(GL_TEXTURE_2D); glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_ALPHA_TEST);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,256,0,256,-1,1);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  glColor4f(1,1,1,1);
  glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
}
static void quad(float x0,float y0,float x1,float y1,float z=0){
  float v[12]={x0,y0,z, x1,y0,z, x1,y1,z, x0,y1,z};
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(3,GL_FLOAT,0,v);
  glDrawArrays(GL_QUADS,0,4);
  glDisableClientState(GL_VERTEX_ARRAY);
}

int main(){
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
  SDL_Window*w=SDL_CreateWindow("t",0,0,256,256,SDL_WINDOW_OPENGL);
  SDL_GLContext c=SDL_GL_CreateContext(w);
  if(!c){printf("no ctx\n");return 2;}
  if(!gles2compat::init((gles2compat::LoaderFn)SDL_GL_GetProcAddress)){ printf("init failed\n"); return 3; }
  glViewport(0,0,256,256);

  printf("[1] flat colored quad, ortho\n");
  reset2D(); glColor4f(1,0,0,1); quad(64,64,192,192);
  dump("center",px(128,128)); dump("outside",px(10,10));
  CHECK(near(px(128,128),255,0,0),"center not red");
  CHECK(near(px(10,10),0,0,255),"outside not blue");

  printf("[2] translate + push/pop\n");
  reset2D(); glColor4f(0,1,0,1);
  glPushMatrix(); glTranslatef(100,0,0); quad(0,0,50,50); glPopMatrix();
  quad(0,200,50,250);
  dump("translated",px(120,20)); dump("orig-pos-empty",px(20,20)); dump("popped",px(20,220));
  CHECK(near(px(120,20),0,255,0),"translated quad missing");
  CHECK(near(px(20,20),0,0,255),"quad drawn without translation");
  CHECK(near(px(20,220),0,255,0),"pop failed");

  printf("[3] color array, GL_QUADS, ubyte colors\n");
  reset2D();
  { float v[12]={0,0,0, 256,0,0, 256,256,0, 0,256,0};
    unsigned char col[16]={255,255,0,255, 255,255,0,255, 255,255,0,255, 255,255,0,255};
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3,GL_FLOAT,0,v); glColorPointer(4,GL_UNSIGNED_BYTE,0,col);
    glDrawArrays(GL_QUADS,0,4);
    glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY); }
  dump("c",px(128,128)); CHECK(near(px(128,128),255,255,0),"color array not applied");

  printf("[4] texture + texcoord, nearest, modulate\n");
  reset2D();
  { unsigned char tex[2*2*4]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,tex);
    glEnable(GL_TEXTURE_2D);
    float v[12]={0,0,0, 256,0,0, 256,256,0, 0,256,0};
    float uv[8]={0,0, 1,0, 1,1, 0,1};
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,uv);
    glColor4f(1,1,1,1);
    glDrawArrays(GL_QUADS,0,4);
    dump("bl(texel0 red)",px(64,64)); dump("br(texel1 green)",px(192,64)); dump("tl(texel2 blue)",px(64,192));
    CHECK(near(px(64,64),255,0,0),"texel0"); CHECK(near(px(192,64),0,255,0),"texel1"); CHECK(near(px(64,192),0,0,255),"texel2");
    glColor4f(0.5f,0.5f,0.5f,1); glDrawArrays(GL_QUADS,0,4);
    dump("modulated",px(64,64)); CHECK(near(px(64,64),128,0,0,4),"modulate");
    glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);
  }

  printf("[5] alpha test\n");
  reset2D();
  { unsigned char tex[4]={255,255,255,0}; // fully transparent texel
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,tex);
    glEnable(GL_TEXTURE_2D); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,0.1f);
    float v[12]={0,0,0, 256,0,0, 256,256,0, 0,256,0}; float uv[8]={0,0,1,0,1,1,0,1};
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3,GL_FLOAT,0,v); glTexCoordPointer(2,GL_FLOAT,0,uv);
    glDrawArrays(GL_QUADS,0,4);
    dump("discarded",px(128,128)); CHECK(near(px(128,128),0,0,255),"alpha test did not discard");
    glDisable(GL_ALPHA_TEST); glDrawArrays(GL_QUADS,0,4);
    dump("not discarded",px(128,128)); CHECK(near(px(128,128),255,255,255),"without alpha test should draw white");
    glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_VERTEX_ARRAY); glDisable(GL_TEXTURE_2D);
  }

  printf("[6] blending\n");
  reset2D(); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(1,0,0,0.5f); quad(0,0,256,256);
  dump("blend",px(128,128)); CHECK(near(px(128,128),128,0,128,4),"blend");

  printf("[7] linear fog (perspective)\n");
  reset2D();
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-1,1,-1,1,1,100);
  glMatrixMode(GL_MODELVIEW); glLoadIdentity();
  glEnable(GL_FOG); glFogi(GL_FOG_MODE,GL_LINEAR); glFogf(GL_FOG_START,10); glFogf(GL_FOG_END,30);
  float fc[4]={0,1,1,1}; glFogfv(GL_FOG_COLOR,fc);
  glColor4f(1,0,0,1);
  quad(-5,-5,5,5,-10); dump("fog@10 (none)",px(128,128)); CHECK(near(px(128,128),255,0,0,8),"fog at start");
  glClear(GL_COLOR_BUFFER_BIT); quad(-50,-50,50,50,-20); dump("fog@20 (half)",px(128,128));
  CHECK(near(px(128,128),128,128,128,10),"fog half");
  glClear(GL_COLOR_BUFFER_BIT); quad(-90,-90,90,90,-40); dump("fog@40 (full)",px(128,128));
  CHECK(near(px(128,128),0,255,255,6),"fog full");
  glDisable(GL_FOG);

  printf("[8] depth test\n");
  reset2D(); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
  glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,256,0,256,-10,10); glMatrixMode(GL_MODELVIEW);
  glColor4f(1,0,0,1); quad(0,0,256,256,-1);  // near? z=-1 -> depth 0.55
  glColor4f(0,1,0,1); quad(0,0,256,256,-5);  // farther, should fail
  dump("depth",px(128,128)); CHECK(near(px(128,128),255,0,0),"depth test failed");
  glColor4f(0,1,0,1); quad(0,0,256,256,3);   // nearer (z=3 -> closer to viewer? ortho -z forward)
  dump("depth2",px(128,128));
  glDisable(GL_DEPTH_TEST);

  printf("[9] display lists\n");
  reset2D();
  GLuint L=glGenLists(1); glNewList(L,GL_COMPILE); glColor4f(1,0,1,1); quad(0,0,40,40); glEndList();
  glPushMatrix(); glTranslatef(100,100,0); glCallList(L); glPopMatrix(); glCallList(L);
  dump("list@translated",px(120,120)); dump("list@origin",px(20,20)); dump("gap",px(80,80));
  CHECK(near(px(120,120),255,0,255),"list translated"); CHECK(near(px(20,20),255,0,255),"list origin"); CHECK(near(px(80,80),0,0,255),"gap");

  printf("[10] lighting w/ normals\n");
  reset2D(); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
  float amb[4]={0.2f,0.2f,0.2f,1}; glLightModelfv(GL_LIGHT_MODEL_AMBIENT,amb);
  float pos[4]={0,0,1,0}; float dif[4]={1,1,1,1}; float zero[4]={0,0,0,1};
  glLightfv(GL_LIGHT0,GL_POSITION,pos); glLightfv(GL_LIGHT0,GL_DIFFUSE,dif); glLightfv(GL_LIGHT0,GL_AMBIENT,zero);
  glColor4f(1,1,1,1);
  { float v[12]={0,0,0, 256,0,0, 256,256,0, 0,256,0};
    glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3,GL_FLOAT,0,v);
    glNormal3f(0,0,1); glDrawArrays(GL_QUADS,0,4); dump("facing light",px(128,128));
    CHECK(px(128,128).r>230,"lit face should be bright");
    glClear(GL_COLOR_BUFFER_BIT); glNormal3f(0,0,-1); glDrawArrays(GL_QUADS,0,4); dump("facing away",px(128,128));
    CHECK(px(128,128).r<80 && px(128,128).r>20,"unlit face should only get ambient (~51)");
    glDisableClientState(GL_VERTEX_ARRAY); }
  glDisable(GL_LIGHTING);

  printf("[11] indexed VBO drawElements\n");
  reset2D();
  { float v[12]={0,0,0, 256,0,0, 256,256,0, 0,256,0}; unsigned short idx[6]={0,1,2,0,2,3};
    GLuint b[2]; glGenBuffers(2,b);
    glBindBuffer(GL_ARRAY_BUFFER,b[0]); glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[1]); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
    glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3,GL_FLOAT,0,(const void*)0);
    glColor4f(0,1,1,1);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,nullptr);
    glDisableClientState(GL_VERTEX_ARRAY); glBindBuffer(GL_ARRAY_BUFFER,0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    dump("vbo",px(128,128)); CHECK(near(px(128,128),0,255,255),"indexed VBO draw"); }


  printf("[12] software cursor overlay + state restore\n");
  reset2D();
  glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
  glColor4f(0.2f,0.6f,0.2f,1); quad(0,0,256,256);
  glDisable(GL_BLEND); // game state at the moment of the swap: blend off, depth on
  gles2compat::drawCursor(100,60,256,256,4.0f); // y from the top
  { std::vector<unsigned char> img(256*256*4); glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,img.data());
    FILE*f=fopen("/tmp/cursor.ppm","wb"); fprintf(f,"P6\n256 256\n255\n");
    for(int y=255;y>=0;y--) for(int x=0;x<256;x++){ fwrite(&img[(y*256+x)*4],1,3,f);} fclose(f); }
  // hot spot pixel: 60px from top => GL y = 255-60-... just inside the tip
  Px tip=px(112,255-92); dump("inside arrow head (white)",tip); CHECK(near(tip,255,255,255),"cursor fill missing");
  Px far=px(200,30); dump("far from cursor",far); CHECK(near(far,51,153,255,3),"background touched");
  CHECK(glIsEnabledProbe(),"state");

  GLenum e; int errs=0; while((e=glGetError())!=GL_NO_ERROR){ printf("  GL error 0x%x\n",e); errs++; }
  CHECK(errs==0,"GL errors pending");
  gles2compat::shutdown();
  printf(failures? "\n%d FAILURES\n":"\nALL PASSED\n",failures);
  return failures?1:0;
}

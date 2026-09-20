GLES2 compat smoke test (runs on any machine with SDL2 + Mesa GLES2):
g++ -std=c++14 gles2_smoke.cpp ../src/pc/webos/GLES2Compat.cpp ../src/pc/webos/WebOSLog.cpp \
    ../external/glad/src/glad.c -I../src/pc -I../external/glad/include -I<SDL2 include> -lSDL2 -lpthread -o smoke
SDL_VIDEODRIVER=offscreen ./smoke

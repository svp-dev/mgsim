#ifdef USE_SDL

#include <SDL.h>
#include <SDL_video.h>
#define SDL_GetEventType(Event) ((Event).type)
#define SDL_GetEventWinType(Event) ((Event).window.event)
#define SDL_GetEventWinWinID(Event) ((Event).window.windowID)
#define SDL_GetEventWinSizeW(Event) ((Event).window.data1)
#define SDL_GetEventWinSizeH(Event) ((Event).window.data2)
#define SDL_GetEventKeyWinID(Event) ((Event).key.windowID)
#define SDL_GetEventKeySym(Event) ((Event).key.keysym.sym)
#define SDL_HasQuit true

#else
#define SDLK_DOWN 7
#define SDLK_END 4
#define SDLK_ESCAPE 0
#define SDLK_HOME 3
#define SDLK_PAGEDOWN 2
#define SDLK_PAGEUP 1
#define SDLK_SPACE 6
#define SDLK_TAB 5
#define SDLK_UP 8
#define SDLK_r 9
#define SDL_CreateTexture(A,B,C,D,E) 0
#define SDL_CreateWindowAndRenderer(A, B, C, D, E) (-1)
#define SDL_DestroyRenderer(A) ((void)0)
#define SDL_DestroyTexture(A) ((void)0)
#define SDL_DestroyWindow(A) ((void)0)
#define SDL_Event int
#define SDL_GetDisplayBounds(A, B) (-1)
#define SDL_GetError() "SDL disabled by configuration"
#define SDL_GetEventKeySym(Event) 0
#define SDL_GetEventKeyWinID(Event) 0
#define SDL_GetEventType(Event) (-1)
#define SDL_GetEventWinType(Event) 0
#define SDL_GetEventWinSizeH(Event) 0
#define SDL_GetEventWinSizeW(Event) 0
#define SDL_GetEventWinWinID(Event) 0
#define SDL_GetNumVideoDisplays() (0)
#define SDL_GetWindowFlags(A) 0
#define SDL_GetWindowID(A) 0
#define SDL_GetWindowSize(A, B, C) do { *(B) = *(C) = 1; } while(0)
#define SDL_HasQuit false
#define SDL_INIT_VIDEO 0
#define SDL_Init(A) (-1)
#define SDL_KEYUP 0
#define SDL_PIXELFORMAT_RGB888 0
#define SDL_PollEvent(A) (((void)(A)), 0)
#define SDL_Quit ((void (*)(void))(void*)1)
struct SDL_Rect { int w; int h; };
#define SDL_RenderCopy(A, B, C, D) ((void)0)
#define SDL_RenderPresent(A) ((void)0)
#define SDL_RenderSetScale(A, B, C) ((void)0)
#define SDL_Renderer int
#define SDL_SetWindowSize(A, B, C) ((void)0)
#define SDL_SetWindowTitle(A, B) ((void)0)
#define SDL_TEXTUREACCESS_STREAMING 0
#define SDL_Texture int
#define SDL_UpdateTexture(A, B, C, D) ((void)0)
#define SDL_WINDOWEVENT 1
#define SDL_WINDOWEVENT_CLOSE 0
#define SDL_WINDOWEVENT_RESIZED 1
#define SDL_WINDOW_SHOWN 0
#define SDL_Window int

#endif

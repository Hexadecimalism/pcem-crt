#include <SDL2/SDL.h>
#define BITMAP WINDOWS_BITMAP
#include <SDL2/SDL_opengl.h>
#undef BITMAP

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ibm.h"
#include "video.h"
#include "wx-sdl2-video.h"

#define CRTEMU_PC_IMPLEMENTATION
#define CRTEMU_PC_SDL
#include "crtemu/crtemu_pc.h"
#include "crtemu/crt_frame_pc.h"

static SDL_GLContext context = NULL;
static crtemu_pc_t* crtemu = NULL;

static uint32_t* frame = NULL;
static int frame_w = 0;
static int frame_h = 0;

extern int video_vsync;

int crt_init(SDL_Window* window, sdl_render_driver requested_render_driver, SDL_Rect screen)
{
        strcpy(current_render_driver_name, requested_render_driver.name);
        
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        
        context = SDL_GL_CreateContext(window);
        if(!context)
        {
                pclog("Could not create GL context.\n");
                return SDL_FALSE;
        }
        SDL_GL_SetSwapInterval(video_vsync ? 1 : 0);

        crtemu = crtemu_pc_create(NULL);
        if(!crtemu)
        {
                pclog("Could not create crtemu context.\n");
                return SDL_FALSE;
        }
        crtemu_pc_frame(crtemu, (unsigned int*)a_crt_frame, 1024, 1024);

        return SDL_TRUE;
}

void crt_close()
{
        free(frame);
        frame   = NULL;
        frame_w = 0;
        frame_h = 0;

        if(crtemu)
        {
                crtemu_pc_destroy(crtemu);
                crtemu = NULL;
        }
        if(context)
        {
                SDL_GL_DeleteContext(context);
                context = NULL;
        }
}

void crt_update(SDL_Window* window, SDL_Rect updated_rect, BITMAP* screen)
{
        if(frame_w != updated_rect.w || frame_h != updated_rect.h)
        {
                frame_w = updated_rect.w;
                frame_h = updated_rect.h;
                free(frame);
                frame = malloc(frame_w * frame_h * 4);
        }
        const uint32_t* screen_ptr = &((uint32_t*)screen->dat)[updated_rect.y * screen->w + updated_rect.x];
        for(int row=0; row<frame_h; row++)
        {
                memcpy(&frame[row*frame_w], &screen_ptr[row*screen->w], frame_w * 4);
        }
}

void crt_present(SDL_Window* window, SDL_Rect video_rect, SDL_Rect window_rect, SDL_Rect screen)
{
        unsigned long long t = SDL_GetTicks() * 1000;
        glViewport(window_rect.x, window_rect.y, window_rect.w, window_rect.h);
        crtemu_pc_present(crtemu, t, frame, frame_w, frame_h, GL_BGRA, 0xFFFFFF, 0x181818);
        SDL_GL_SwapWindow(window);
}

sdl_renderer_t* crt_renderer_create()
{
        sdl_renderer_t* renderer = malloc(sizeof(sdl_renderer_t));
        renderer->init = crt_init;
        renderer->close = crt_close;
        renderer->update = crt_update;
        renderer->present = crt_present;
        renderer->always_update = 0;
        return renderer;
}

void crt_renderer_close(sdl_renderer_t* renderer)
{
        free(renderer);
}

int crt_renderer_available(struct sdl_render_driver* driver)
{
        return 1;
}

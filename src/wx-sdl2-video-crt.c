#include <SDL2/SDL.h>
#define BITMAP WINDOWS_BITMAP
#include <SDL2/SDL_opengl.h>
#undef BITMAP

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ibm.h"
#include "video.h"
#include "wx-sdl2.h"
#include "wx-sdl2-video.h"
#include "wx-utils.h"

#define CRTEMU_PC_IMPLEMENTATION
#define CRTEMU_PC_SDL
#include "crtemu/crtemu_pc.h"

static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;

static SDL_GLContext context = NULL;
static crtemu_pc_t* crtemu = NULL;
static GLuint screen_tex = 0;
static GLuint screen_fbo = 0;
static int last_video_w = 0;
static int last_video_h = 0;

extern int video_vsync;
extern int video_focus_dim;

int crt_monitor_frame = 1;

static void crt_load_frame(void* img)
{
        int frame_w, frame_h;
        wx_image_get_size(img, &frame_w, &frame_h);
        const unsigned char* frame_rgb = wx_image_get_data(img);
        const unsigned char* frame_alpha = wx_image_get_alpha(img);

        unsigned char* frame_rgba = malloc(frame_w * frame_h * 4);
        for(int i=0; i<frame_w*frame_h; ++i)
        {
                frame_rgba[4*i+0] = frame_rgb[3*i+0];
                frame_rgba[4*i+1] = frame_rgb[3*i+1];
                frame_rgba[4*i+2] = frame_rgb[3*i+2];
                frame_rgba[4*i+3] = frame_alpha[i];
        }
        crtemu_pc_frame(crtemu, (unsigned int*)frame_rgba, frame_w, frame_h);
        free(frame_rgba);
}

static void crt_take_screenshot(SDL_Window* window)
{
        int width, height;
        SDL_GL_GetDrawableSize(window, &width, &height);

        unsigned char* pixels_rgba = malloc(width * height * 4);
        unsigned char* pixels_rgb  = malloc(width * height * 3);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels_rgba);
        for(int y=0; y<height; ++y)
        {
                for(int x=0; x<width; ++x)
                {
                        pixels_rgb[3*(y*width+x)+0] = pixels_rgba[4*((height-y-1)*width+x)+0];
                        pixels_rgb[3*(y*width+x)+1] = pixels_rgba[4*((height-y-1)*width+x)+1];
                        pixels_rgb[3*(y*width+x)+2] = pixels_rgba[4*((height-y-1)*width+x)+2];
                }
        }
        free(pixels_rgba);
        screenshot_taken(pixels_rgb, width, height);
        free(pixels_rgb);
}

int crt_init(SDL_Window* window, sdl_render_driver requested_render_driver, SDL_Rect screen)
{
        strcpy(current_render_driver_name, requested_render_driver.name);
        
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        
        context = SDL_GL_CreateContext(window);
        if(!context)
        {
                pclog("Could not create GL context.\n");
                return SDL_FALSE;
        }
        glGenFramebuffers = SDL_GL_GetProcAddress("glGenFramebuffers");
        glBindFramebuffer = SDL_GL_GetProcAddress("glBindFramebuffer");
        glDeleteFramebuffers = SDL_GL_GetProcAddress("glDeleteFramebuffers");
        glFramebufferTexture2D = SDL_GL_GetProcAddress("glFramebufferTexture2D");

        SDL_GL_SetSwapInterval(video_vsync ? 1 : 0);
        glEnable(GL_TEXTURE_2D);
        
        crtemu = crtemu_pc_create(NULL);
        if(!crtemu)
        {
                pclog("Could not create crtemu context.\n");
                return SDL_FALSE;
        }

        void* crtemu_frame = wx_image_load_resource("BITMAP_CRT_FRAME");
        if(crtemu_frame)
        {
                crt_load_frame(crtemu_frame);
                wx_image_free(crtemu_frame);
        }

        glGenTextures(1, &screen_tex);
        glBindTexture(GL_TEXTURE_2D, screen_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screen.w, screen.h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &screen_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, screen_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screen_tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return SDL_TRUE;
}

void crt_close()
{
        last_video_w = 0;
        last_video_h = 0;
        if(screen_fbo)
        {
                glDeleteFramebuffers(1, &screen_fbo);
                screen_fbo = 0;
        }
        if(screen_tex)
        {
                glDeleteTextures(1, &screen_tex);
                screen_tex = 0;
        }
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
        const uint32_t* pixels = &((uint32_t*)screen->dat)[updated_rect.y * screen->w + updated_rect.x];
        glBindTexture(GL_TEXTURE_2D, screen_tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, screen->w);
        glTexSubImage2D(GL_TEXTURE_2D, 0, updated_rect.x, updated_rect.y, updated_rect.w, updated_rect.h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels );
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
}

void crt_present(SDL_Window* window, SDL_Rect video_rect, SDL_Rect window_rect, SDL_Rect screen)
{
        glBindFramebuffer(GL_READ_FRAMEBUFFER, screen_fbo);
        glBindTexture(GL_TEXTURE_2D, crtemu->backbuffer);
        if(video_rect.w != last_video_w || video_rect.h != last_video_h)
        {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, video_rect.w, video_rect.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                last_video_w = video_rect.w;
                last_video_h = video_rect.h;
        }
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, video_rect.x, video_rect.y, video_rect.w, video_rect.h);
        glBindTexture(GL_TEXTURE_2D, 0);

        int dim = !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) && video_focus_dim && !take_screenshot;
        unsigned long long t = SDL_GetTicks() * 1000;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(window_rect.x, window_rect.y, window_rect.w, window_rect.h);
        crtemu->use_frame = crt_monitor_frame ? 1.0f : 0.0f;
        crtemu_pc_present(crtemu, t, video_rect.w, video_rect.h, dim ? 0x808080 : 0xFFFFFF, 0x000000 );

        if(take_screenshot)
        {
                take_screenshot = 0;
                crt_take_screenshot(window);
        }
        
        SDL_GL_SwapWindow(window);
}

sdl_renderer_t* crt_renderer_create()
{
        sdl_renderer_t* renderer = malloc(sizeof(sdl_renderer_t));
        renderer->init = crt_init;
        renderer->close = crt_close;
        renderer->update = crt_update;
        renderer->present = crt_present;
        renderer->always_update = 1;
        return renderer;
}

void crt_renderer_close(sdl_renderer_t* renderer)
{
        free(renderer);
}

static int renderer_available = -1;
int crt_renderer_available(struct sdl_render_driver* driver)
{
        if(renderer_available < 0)
        {
                renderer_available = 0;

                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

                SDL_Window* window = SDL_CreateWindow("", 0, 0, 1, 1, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
                if(window)
                {
                        SDL_GLContext context = SDL_GL_CreateContext(window);
                        if(context)
                        {
                                int glversion = -1;
                                glGetIntegerv(GL_MAJOR_VERSION, &glversion);
                                SDL_GL_DeleteContext(context);
                                renderer_available = glversion >= 3 ? 1 : 0;
                        }
                        SDL_DestroyWindow(window);
                }
        }
        return renderer_available;
}

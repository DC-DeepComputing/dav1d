/*
 * Copyright © 2020, VideoLAN and dav1d authors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dp_renderer.h"
#include "dp_compat.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * Renderer context for SDL
 */
typedef struct renderer_priv_ctx
{
    // SDL window
    SDL_Window *win;
    // SDL renderer
    SDL_Renderer *renderer;
    // Lock protecting access to the texture
    SDL_Mutex *lock;
    // Texture to render
    SDL_Texture *tex;
    // Texture dimensions
    int tex_w, tex_h;
} Dav1dPlayRendererPrivateContext;

static void *sdl_renderer_create(const Dav1dPlaySettings *settings)
{
    SDL_Window *win = dp_create_sdl_window(0, settings->fullscreen);
    if (win == NULL) {
        fprintf(stderr, "Creating SDL window failed: %s\n", SDL_GetError());
        return NULL;
    }

    // Alloc
    Dav1dPlayRendererPrivateContext *rd_priv_ctx = malloc(sizeof(Dav1dPlayRendererPrivateContext));
    if (rd_priv_ctx == NULL) {
        fprintf(stderr, "Out of memory!\n");
        return NULL;
    }
    rd_priv_ctx->win = win;

#if SDL_VERSION_ATLEAST(3, 0, 0)
    // Create renderer
    rd_priv_ctx->renderer = SDL_CreateRenderer(win, NULL);
#else
    // Create renderer
    rd_priv_ctx->renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    // Set scale quality
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
#endif

    // Create Mutex
    rd_priv_ctx->lock = SDL_CreateMutex();
    if (rd_priv_ctx->lock == NULL) {
        fprintf(stderr, "SDL_CreateMutex failed: %s\n", SDL_GetError());
        free(rd_priv_ctx);
        return NULL;
    }

    rd_priv_ctx->tex = NULL;
    rd_priv_ctx->tex_w = 0;
    rd_priv_ctx->tex_h = 0;

    return rd_priv_ctx;
}

static void sdl_renderer_destroy(void *cookie)
{
    Dav1dPlayRendererPrivateContext *rd_priv_ctx = cookie;
    assert(rd_priv_ctx != NULL);

    SDL_DestroyTexture(rd_priv_ctx->tex);
    SDL_DestroyRenderer(rd_priv_ctx->renderer);
    SDL_DestroyWindow(rd_priv_ctx->win);
    SDL_DestroyMutex(rd_priv_ctx->lock);
    free(rd_priv_ctx);
}

static void sdl_render(void *cookie, const Dav1dPlaySettings *settings)
{
    Dav1dPlayRendererPrivateContext *rd_priv_ctx = cookie;
    assert(rd_priv_ctx != NULL);

    SDL_LockMutex(rd_priv_ctx->lock);

    if (rd_priv_ctx->tex == NULL) {
        SDL_UnlockMutex(rd_priv_ctx->lock);
        return;
    }

    // Display the frame
    SDL_RenderClear(rd_priv_ctx->renderer);
#if SDL_VERSION_ATLEAST(3, 0, 0)
    SDL_RenderTexture(rd_priv_ctx->renderer, rd_priv_ctx->tex, NULL, NULL);
#else
    SDL_RenderCopy(rd_priv_ctx->renderer, rd_priv_ctx->tex, NULL, NULL);
#endif
    SDL_RenderPresent(rd_priv_ctx->renderer);

    SDL_UnlockMutex(rd_priv_ctx->lock);
}

static int sdl_update_texture(void *cookie, Dav1dPicture *dav1d_pic,
                              const Dav1dPlaySettings *settings)
{
    Dav1dPlayRendererPrivateContext *rd_priv_ctx = cookie;
    assert(rd_priv_ctx != NULL);

    SDL_LockMutex(rd_priv_ctx->lock);

    if (dav1d_pic == NULL) {
        rd_priv_ctx->tex = NULL;
        SDL_UnlockMutex(rd_priv_ctx->lock);
        return 0;
    }

    int width = dav1d_pic->p.w;
    int height = dav1d_pic->p.h;

    enum Dav1dPixelLayout dav1d_layout = dav1d_pic->p.layout;

    if (DAV1D_PIXEL_LAYOUT_I420 != dav1d_layout || dav1d_pic->p.bpc != 8) {
        fprintf(stderr, "Unsupported pixel format, only 8bit 420 supported so far.\n");
        exit(50);
    }

    SDL_Texture *texture = rd_priv_ctx->tex;
    if (texture != NULL) {
        if (rd_priv_ctx->tex_w != width || rd_priv_ctx->tex_h != height) {
            SDL_DestroyTexture(texture);
            texture = NULL;
        }
    }

    if (texture == NULL) {
        texture = SDL_CreateTexture(rd_priv_ctx->renderer, SDL_PIXELFORMAT_IYUV,
            SDL_TEXTUREACCESS_STREAMING, width, height);
#if SDL_VERSION_ATLEAST(3, 0, 0)
        SDL_SetRenderLogicalPresentation(rd_priv_ctx->renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
#else
        SDL_RenderSetLogicalSize(rd_priv_ctx->renderer, width, height);
#endif
    }

    SDL_UpdateYUVTexture(texture, NULL,
        dav1d_pic->data[0], (int)dav1d_pic->stride[0], // Y
        dav1d_pic->data[1], (int)dav1d_pic->stride[1], // U
        dav1d_pic->data[2], (int)dav1d_pic->stride[1]  // V
        );

    rd_priv_ctx->tex = texture;
    rd_priv_ctx->tex_w = width;
    rd_priv_ctx->tex_h = height;
    SDL_UnlockMutex(rd_priv_ctx->lock);
    return 0;
}

const Dav1dPlayRenderInfo rdr_sdl = {
    .name = "sdl",
    .create_renderer = sdl_renderer_create,
    .destroy_renderer = sdl_renderer_destroy,
    .render = sdl_render,
    .update_frame = sdl_update_texture
};

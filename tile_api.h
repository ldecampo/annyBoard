#ifndef TILE_API_H
#define TILE_API_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>  

#ifdef __cplusplus
extern "C" {
#endif

#define TILE_API_VERSION 2 //im gonna change this every time i change this file

typedef struct TileContext {
    SDL_Renderer *renderer;
    int screen_w;
    int screen_h;

    //fonts are owned by the host app, tiles just borrow pointers.
    TTF_Font *font_small;
    TTF_Font *font_medium;
} TileContext;

typedef struct Tile {
    int api_version;

    const char* (*name)(void);

    //plugin_dir is the folder containing this plugin's .so and JSON.
    void* (*create)(TileContext *ctx, const char *plugin_dir);

    void  (*destroy)(void *state);

    //dt = downtime
    void  (*update)(void *state, double dt);

    //function called once whenever this tile becomes the active tile
    void  (*on_show)(void *state);

    //render into "rect"
    void  (*render)(void *state, SDL_Renderer *r, const SDL_Rect *rect);

    double (*preferred_duration)(void);
} Tile;

typedef const Tile* (*tile_get_fn)(void);

#ifdef __cplusplus
}
#endif

#endif
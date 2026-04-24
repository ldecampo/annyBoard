#include "../../tile_api.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*

compile commmand:
gcc -fPIC -shared -o plugins/plugin3/plugin3.so \
  plugins/plugin3/plugin3.c \
  `sdl2-config --cflags --libs` -lSDL2_ttf -lSDL2_image -lm

*/

typedef struct {
    TileContext *ctx; //borrowed
    char plugin_dir[512]; //where state.json lives
    int prime;
    char last_date[16]; //"YYYY-MM-DD"

    int a; //random element in {1..p-1} for current display
    int inv; //inverse of a mod p
    int has_choice;

    SDL_Texture *t1; int t1w, t1h;
    SDL_Texture *t2; int t2w, t2h;
    SDL_Texture *t3; int t3w, t3h;
    SDL_Texture *t4; int t4w, t4h;
} State;

typedef struct {
    char description[128];
    float temp;
    float feels_like;
    int pressure;
    int humidity;
    float wind_speed;

    char temp_unit[8];
    char pressure_unit[8];
    char wind_unit[8];

} Weather;

typedef struct {
	char *response;
	size_t size;
} CURL_Critter;

//This is important, it makes our char*s into textures
static SDL_Texture* make_text(SDL_Renderer *r, TTF_Font *font,
                              const char *msg, int *w, int *h) {
    SDL_Color white = {255,255,255,255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, msg, white);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    *w = surf[0].w; *h = surf[0].h;
    SDL_FreeSurface(surf);
    return tex;
}

//Free textures
static void free_texts(State *s) {
    if (s[0].t1) SDL_DestroyTexture(s[0].t1);
    if (s[0].t2) SDL_DestroyTexture(s[0].t2);
    if (s[0].t3) SDL_DestroyTexture(s[0].t3);
    if (s[0].t4) SDL_DestroyTexture(s[0].t4);
    s[0].t1=s[0].t2=s[0].t3=s[0].t4=NULL;
}

static size_t download(char *data, size_t size, size_t nmemb, void *clientp) {
	size_t realsize = nmemb;
	CURL_Critter *mem = (CURL_Critter *)clientp;
	
	char *ptr = realloc(mem->response, mem->size + realsize + 1);
	if (!ptr) {
		return 0;
	}

	mem->response = ptr;
	memcpy(&(mem->response[mem->size]), data, realsize);
	mem->size += realsize;
	mem->response[mem->size] = 0;
	return realsize;
}

static void parse_weather(Weather *w, CURL_Critter *c) {
	xmlDoc *doc = xmlParseDoc((xmlChar *)c->response);
	//xmlNodePtr root = NULL;
	xmlNodePtr root = xmlDocGetRootElement(doc);
	if (root != NULL) {	
		for (xmlNodePtr cur = root->children; cur != NULL; cur = cur->next) {
			// evil else-if of doom
			if (strcmp(cur->name, "temperature") == 0) {
				w->temp = atof(xmlGetProp(cur, "value"));
				xmlChar *unit_name = xmlGetProp(cur, "unit");
				if (strcmp(unit_name, "fahrenheit") == 0) {
					strcpy(w->temp_unit, "F");
				} else if (strcmp(unit_name, "celsius") == 0) {
					strcpy(w->temp_unit, "C");
				} else if (strcmp(unit_name, "kelvin") == 0) {
					strcpy(w->temp_unit, "K");
				}
			} else if (strcmp(cur->name, "feels_like") == 0) {
				w->feels_like = atof(xmlGetProp(cur, "value"));
			} else if (strcmp(cur->name, "humidity") == 0) {
				w->humidity = atoi(xmlGetProp(cur, "value"));
			} else if (strcmp(cur->name, "pressure") == 0) {
				w->pressure = atoi(xmlGetProp(cur, "value"));
				strcpy(w->pressure_unit,  xmlGetProp(cur, "unit"));
			} else if (strcmp(cur->name, "wind") == 0) {
				for (xmlNodePtr windChild = cur->children; windChild != NULL; windChild = windChild->next) {
					if (strcmp(windChild->name, "speed") == 0) {
						w->wind_speed = atof(xmlGetProp(windChild, "value"));
						strcpy(w->wind_unit, xmlGetProp(windChild, "unit"));
					}
				}
			}
		}

	} else {
		fprintf(stderr, "Unable to load weather data! Is there a connection to the internet?\n");
	}
}
static Weather* get_weather(State *s) {
    Weather *w = malloc(sizeof(Weather));
    w->feels_like = -1.0;
    w->humidity = -1;
    w->pressure = -1;
    w->temp = -1;
    w->wind_speed = -1.0;
    strcpy(w->description, "");
    strcpy(w->temp_unit, "");
    strcpy(w->pressure_unit, "");
    strcpy(w->wind_unit, "");

    FILE *apiptr;
    char api_key_path[1024];
    snprintf(api_key_path, sizeof(api_key_path), "%s/api-key.txt", s[0].plugin_dir);
    apiptr = fopen(api_key_path, "r");

    if (apiptr != NULL) {
        fprintf(stderr, "Weather Tile: API key sucessfully loaded!\n");
        char api_key[128];
        fgets(api_key, 128, apiptr);
        api_key[strcspn(api_key, "\n")] = 0;

        FILE *propsptr;
        char props_path[1024];
        snprintf(props_path, sizeof(props_path), "%s/weather.properties", s[0].plugin_dir);
        propsptr = fopen(props_path, "r");

        if (propsptr != NULL) {
            CURL *curl = curl_easy_init();
            if(curl) {
                CURLcode result;
                char api_url[1024];

                float lat = 0.0;
                float lon = 0.0;
                char *units = malloc(sizeof(char) * 64);
                strcpy(units, "imperial");

                char line[256];
                while (fgets(line, sizeof(line), propsptr)) {
                    char *name = strtok(line, ":");
                    char *value = strtok(NULL, ":");

                    value[strcspn(value, "\n")] = 0;

                    if (strcmp(name, "lat") == 0) {
                        lat = atof(value);
                    } else if (strcmp(name, "lon") == 0) {
                        lon = atof(value);
                    } else if (strcmp(name, "units") == 0) {
                        strcpy(units, value);
                    }
                }

                snprintf(api_url, 1024, "https://api.openweathermap.org/data/2.5/weather?mode=xml&lat=%0.1f&lon=%0.1f&units=%s&appid=%s", lat, lon, units, api_key);
                
		CURL_Critter ccritter = { 0 };
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&ccritter);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download);
                curl_easy_setopt(curl, CURLOPT_URL, api_url);
                result = curl_easy_perform(curl);
                curl_easy_cleanup(curl);

		parse_weather(w, &ccritter);

                free(units);
		free(ccritter.response);
            }
            fclose(propsptr);
        }
        fclose(apiptr);
    } else {
        fprintf(stderr, "Weather Tile: Unable to open weather API key file.\n");
    }


    return w;
}

//Rebuild on reload
static void rebuild_texts(State *s) {
    free_texts(s);

    SDL_Renderer *r = s[0].ctx[0].renderer;

    char line1[128];
    char line2[128];
    char line3[128];
    char line4[128];

    Weather *w = get_weather(s);

    // TODO: respect units in the config

    snprintf(line1, sizeof(line1), "Weather | %s", w->description);
    snprintf(line2, sizeof(line2), "%0.1f°%s | Feels like: %0.1f°%s", w->temp, w->temp_unit, w->feels_like, w->temp_unit);
    snprintf(line3, sizeof(line3), "%d%s | %d%% Humidity", w->pressure, w->pressure_unit, w->humidity);
    snprintf(line4, sizeof(line4), "%0.1f%s wind", w->wind_speed, w->wind_unit);

    s[0].t1 = make_text(r, s[0].ctx[0].font_medium, line1, &s[0].t1w, &s[0].t1h);
    s[0].t2 = make_text(r, s[0].ctx[0].font_small,  line2, &s[0].t2w, &s[0].t2h);
    s[0].t3 = make_text(r, s[0].ctx[0].font_small,  line3, &s[0].t3w, &s[0].t3h);
    s[0].t4 = make_text(r, s[0].ctx[0].font_small,  line4, &s[0].t4w, &s[0].t4h);
    
    free(w);


}

static const char* tile_name(void) { return "Weather"; }

static void* tile_create(TileContext *ctx, const char *plugin_dir) {
    State *s = (State*)SDL_calloc(1, sizeof(State));
    s[0].ctx = ctx;
    strncpy(s[0].plugin_dir, plugin_dir, sizeof(s[0].plugin_dir)-1);



    s[0].has_choice = 0;
    rebuild_texts(s);
    return s;
}

static void tile_destroy(void *state) {
    State *s = (State*)state;
    free_texts(s);
    SDL_free(s);
}

static void tile_update(void *state, double dt) {
    (void)dt;
    //Nothing time-animated in this tile right now.
}

static void tile_on_show(void *st) {
    State *s = (State*)st;
    rebuild_texts(s);
    fprintf(stderr, "Weather Tile: on_show state=%p\n", st);
}


static void tile_render(void *state, SDL_Renderer *r, const SDL_Rect *rect) {
    State *s = (State*)state;

    //use a soft panel background so its distinguished from the main background
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 70);
    SDL_RenderFillRect(r, rect);

    //thin border
    SDL_SetRenderDrawColor(r, 255, 255, 255, 90);
    SDL_RenderDrawRect(r, rect);

    //simple layout
    int pad = rect[0].w / 18;
    int x = rect[0].x + pad;
    int y = rect[0].y + pad;

    if (s[0].t1) { SDL_Rect d = {x, y, s[0].t1w, s[0].t1h}; SDL_RenderCopy(r, s[0].t1, NULL, &d); y += s[0].t1h + pad/2; }
    if (s[0].t2) { SDL_Rect d = {x, y, s[0].t2w, s[0].t2h}; SDL_RenderCopy(r, s[0].t2, NULL, &d); y += s[0].t2h + pad/3; }
    if (s[0].t3) { SDL_Rect d = {x, y, s[0].t3w, s[0].t3h}; SDL_RenderCopy(r, s[0].t3, NULL, &d); y += s[0].t3h + pad/3; }
    if (s[0].t4) { SDL_Rect d = {x, y, s[0].t4w, s[0].t4h}; SDL_RenderCopy(r, s[0].t4, NULL, &d); }
}

//8 seconds is plenty long
static double tile_pref_duration(void) { return 8.0; }

//here's the thing we care about
static Tile TILE = {
    .api_version = TILE_API_VERSION,
    .name = tile_name,
    .create = tile_create,
    .destroy = tile_destroy,
    .update = tile_update,
    .render = tile_render,
    .preferred_duration = tile_pref_duration,
    .on_show = tile_on_show,
    .on_hide = NULL
};

//give the tile to the main thing
const Tile* tile_get(void) { return &TILE; }

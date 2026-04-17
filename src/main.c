#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <libdragon.h>
#include <dir.h>

#include "boot/boot.h"
#include "flashcart/flashcart.h"
#include "utils/fs.h"

#define PI 3.141592653f
#define PI2 (PI*2.0f)

#define RDPQ_COMBINER_TEX_ALPHA   RDPQ_COMBINER1((0,0,0,PRIM),    (TEX0,0,PRIM,0))

int menu_active;
boot_params_t boot_params;
char rom_path[1024];

#define ROM_CACHE_MAGIC      0x4E534F43u
#define ROM_CACHE_VERSION    1u
#define ROMS_DIRECTORY       "sd:/roms"
#define ROM_CACHE_PATH       "sd:/menu/rom_cache.bin"
#define ROM_CACHE_TMP_PATH   "sd:/menu/rom_cache.tmp"
#define BOXART_CACHE_SIZE    24

float lerp(float t, float a, float b) {
    return a + (b - a) * t;
}

float bezier_interp(float t, float x1, float y1, float x2, float y2) {
    float f0 = 1.0f - 3.0f * x2 + 3 * x1;
    float f1 = 3.0f * x2 - 6.0f * x1;
    float f2 = 3.0f * x1;
    float refinedT = t;

    for (int i = 0; i < 5; i++) {
        float refinedT2 = refinedT * refinedT;
        float refinedT3 = refinedT2 * refinedT;

        float x = f0 * refinedT3 + f1 * refinedT2 + f2 * refinedT;
        float slope = 1.0f / (3.0f * f0 * refinedT2 + 2.0f * f1 * refinedT + f2);
        refinedT -= (x - t) * slope;
        if(refinedT < 0.0f) refinedT = 0.0f;
        if(refinedT > 1.0f) refinedT = 1.0f;
    }

    // Resolve cubic bezier for the given x
    return 3.0f * powf(1.0f - refinedT, 2.0f) * refinedT * y1 + 3.0f * (1.0f - refinedT) * powf(refinedT, 2.0f) * y2 + powf(refinedT, 3.0f);
}

sprite_t *logo;
sprite_t *shadow;
sprite_t *loading_dot;
sprite_t *placeholder_n64;
sprite_t *loading_splash;

int spinner_fade_counter = 0;
float spinner_fade = 0.0f;
float spinner_counter = 0.0f;

void spinner_draw(float x, float y, float size, float alpha) {
    if(loading_dot == NULL) {
        return;
    }

    float dot_size = (11.0f * 0.5f) * 0.4f;
    rdpq_set_prim_color(RGBA32(255, 255, 255, alpha * 255.0f));
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_ALPHA);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    for(int a = 11; a >= 0 ; a--) {
        float thisAnimSize = (12.0f - fmodf((spinner_counter + a),12.0f))- 9.0f;
        if(thisAnimSize < 0.0f) thisAnimSize = 0.0f;
        float animParam = thisAnimSize / 3.0f;
        float scaled = 0.4f + (animParam * 0.6f);
        float dot_ang = (a / 12.0f) * PI2;
        float dot_x = (x + sinf(dot_ang) * size) - dot_size * scaled;
        float dot_y = (y + cosf(dot_ang) * size) - dot_size * scaled;
        rdpq_sprite_blit(loading_dot, dot_x, dot_y, &(rdpq_blitparms_t){
            .scale_x = scaled, .scale_y = scaled,
        });
    }
    spinner_counter += 0.25f;
}
        
static void cart_load_progress(float progress) {
    surface_t *d = (progress >= 1.0f) ? display_get() : display_try_get();
    if (d) {
        rdpq_attach(d, NULL);

        rdpq_set_mode_fill(RGBA32(0,0,0,0));
        rdpq_fill_rectangle(0, 0, 640, 480);

        rdpq_set_mode_standard();
        spinner_fade_counter++;
        if(spinner_fade_counter >= 10) {
            spinner_fade = (spinner_fade_counter - 10) / 30.0f;
            if(spinner_fade > 1.0f) spinner_fade = 1.0f;
            spinner_draw(640 - 55, 480 - 48, 20, spinner_fade);
        }
        rdpq_detach_show();
    }
    
}

float fade_v1[] = { 0, 0 };
float fade_v2[] = { 640, 480 };
float fade_v3[] = { 0, 480 };
float fade_v4[] = { 640, 0 };

joypad_buttons_t p1_buttons;
joypad_buttons_t p1_buttons_press;
joypad_inputs_t p1_inputs;

rdpq_font_t * font;

static wav64_t se_titlelogo;
static wav64_t se_cursor_ng;
static wav64_t se_gametitle_cursor;
static wav64_t se_filemenu_close;
static wav64_t se_filemenu_open;
static wav64_t se_gametitle_click; 
static bool se_titlelogo_loaded;
static bool se_cursor_ng_loaded;
static bool se_gametitle_cursor_loaded;
static bool se_filemenu_close_loaded;
static bool se_filemenu_open_loaded;
static bool se_gametitle_click_loaded;

static bool rom_asset_exists(const char *path) {
    FILE *asset = fopen(path, "rb");

    if(asset == NULL) {
        return false;
    }

    fclose(asset);
    return true;
}

static sprite_t *sprite_load_optional(const char *path) {
    if(!rom_asset_exists(path)) {
        return NULL;
    }

    return sprite_load(path);
}

static bool wav64_open_optional(wav64_t *wav, const char *path) {
    if(!rom_asset_exists(path)) {
        return false;
    }

    wav64_open(wav, path);
    return true;
}

static void wav64_play_optional(wav64_t *wav, bool loaded, int channel) {
    if(loaded) {
        wav64_play(wav, channel);
    }
}

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

#define CURSOR_INTERVAL 25
#define CURSOR_INTERVAL_SECOND (CURSOR_INTERVAL - 6)

#define FADE_DURATION 60.0f
#define OPENING_WAIT 60.0f * 2.0f
#define OPENING_SOUND_PLAY (OPENING_WAIT - 10.0f)
#define OPENING_OUT 20.0f

#define SCR_REGION_X_MIN 16.0f
#define BOX_REGION_X_MIN 64.0f
#define BOX_REGION_X_MAX 624.0f
#define BOX_REGION_Y_MIN 16.0f
#define BOX_REGION_Y_MAX 464.0f

#define TITLE_BOX_WIDTH_E 256.0f
#define TITLE_BOX_HEIGHT_E 179.0f

#define TITLE_BOX_WIDTH_BORDER_E 257.0f
#define TITLE_BOX_HEIGHT_BORDER_W 180.0f

#define TITLE_SELECT_SCALE 1.2f

#define CHANNEL_SFX1    0
#define CHANNEL_SFX2    2
#define CHANNEL_SFX3    4
#define CHANNEL_MUSIC   6

float box_region_min_x = BOX_REGION_X_MIN;
float box_region_max_x = BOX_REGION_X_MAX;
float box_region_min_y = BOX_REGION_Y_MIN;
float box_region_max_y = BOX_REGION_Y_MAX;

float viewport_y = BOX_REGION_Y_MIN;
float viewport_y_bottom = BOX_REGION_Y_MAX;
float viewport_y_target = BOX_REGION_Y_MIN;

float box_region_scissor_left = 64.0f;

int fade_state = 0;
int fade_counter = FADE_DURATION;
float fade_lvl = 1.0f;

int main_state = 0;
int opening_counter = OPENING_WAIT;
float opening_card_offset_x = 0.0f;
float loading_splash_alpha = 0.0f;

float shadow_vertex[8];

void shadow_draw(float x, float y, float width, float height, float alpha) {
    if(shadow == NULL) {
        return;
    }

    // Load shadow sprite surface
    surface_t surf = sprite_get_pixels(shadow);
    rdpq_set_prim_color(RGBA32(0, 0, 0, alpha * 255.0f));
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_ALPHA);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_dithering(DITHER_BAYER_BAYER);
    rdpq_mode_tlut(TLUT_NONE);
    rdpq_tex_upload(TILE0, &surf, &(rdpq_texparms_t){.s.repeats = 1, .t.repeats = 1});

    float xw = x + width;
    float yh = y + height;

    rdpq_triangle(&TRIFMT_TEX,
        (float[]){ x,  y,  0.0f, 0.0f, 1.0f },
        (float[]){xw,  y, 64.0f, 0.0f, 1.0f },
        (float[]){xw, yh, 64.0f,64.0f, 1.0f }
    );

    rdpq_triangle(&TRIFMT_TEX,
        (float[]){ x,  y,  0.0f, 0.0f, 1.0f },
        (float[]){xw, yh, 64.0f,64.0f, 1.0f },
        (float[]){ x, yh, 64.0f, 0.0f, 1.0f }
    );
    
    // Revert combiner
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
}

void outline_draw(float x, float y, float width, float height, float brightness) {
    #define OUTLINE_WIDTH 1
    int brightv = brightness * 255.0f;
    x = roundf(x);
    y = roundf(y);
    width = ceilf(width);
    height = ceilf(height);
    rdpq_set_mode_fill(RGBA32(brightv,brightv,brightv,0));
    rdpq_fill_rectangle(x, y, x + width, y + OUTLINE_WIDTH);
    rdpq_fill_rectangle(x, y+height-OUTLINE_WIDTH, x + width, y + height);
    rdpq_fill_rectangle(x, y, x+OUTLINE_WIDTH, y+height);
    rdpq_fill_rectangle(x+width-OUTLINE_WIDTH, y, x+width, y+height);
}

typedef struct SquareSprite_s {
    float x, y;
    float width;
    float height;
} SquareSprite;

typedef struct TitleBox_s {
    char id[8];
    char rom_path[256];
    char product_code[5];
    sprite_t * image;
    SquareSprite sprite;
    float scale;
    float scaleGrow;
    float offset_x, offset_y;
    int isSelected;
    float selectedOutline;
    float outlineCounter;
    float screenX;
    float screenY;
} TitleBox;

float title_brightness = 1.0f;

#define TITLE_COLUMNS 4
#define TITLE_ROWS 80
#define TITLE_TOTAL_SLOTS (TITLE_ROWS * TITLE_COLUMNS)
#define MAX_TITLE_COUNT TITLE_TOTAL_SLOTS

typedef struct {
    char rom_path[256];
    char product_code[5];
    char art_id[3];
    int64_t file_size;
} RomScanEntry;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint64_t fingerprint;
    uint32_t entry_count;
} RomCacheHeader;

typedef struct __attribute__((packed)) {
    char rom_path[256];
    char product_code[5];
    char art_id[3];
} RomCacheEntry;

int title_count = 0;

typedef struct {
    char art_id[3];
    sprite_t *sprite;
    uint32_t last_used;
} BoxartCacheEntry;

TitleBox title_list[MAX_TITLE_COUNT];
static RomScanEntry rom_scan_entries[MAX_TITLE_COUNT];
static RomCacheEntry rom_cache_entries[MAX_TITLE_COUNT];
static BoxartCacheEntry boxart_cache[BOXART_CACHE_SIZE];
static uint32_t boxart_cache_clock = 0;

static sprite_t *get_boxart_sprite(const char *art_id);
static void boxart_cache_reset(void);

TitleBox * title_row[TITLE_ROWS][TITLE_COLUMNS] = {
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL}
};
int title_row_count[TITLE_ROWS] = {0};

TitleBox * selectedTitle = NULL;

int cursor_x = 0;
int cursor_x_timer = 0;
int cursor_y = 0;
int cursor_y_timer = 0;

void TitleBox_create(TitleBox * title, sprite_t * image, float x, float y) {
    memset(title->id, 0, sizeof(title->id));
    memset(title->rom_path, 0, sizeof(title->rom_path));
    memset(title->product_code, 0, sizeof(title->product_code));
    title->image = image;
    title->sprite.x = x;
    title->sprite.y = y;
    title->sprite.width = TITLE_BOX_WIDTH_E;
    title->sprite.height = TITLE_BOX_HEIGHT_E;
    title->scale = 1.0f;
    title->offset_x = title->offset_y = 0.0f;
    title->isSelected = 0;
    title->scaleGrow = 0.0f;
    title->selectedOutline = 0.0f;
    title->outlineCounter = 0.0f;
}

void TitleBox_create2(TitleBox * title, sprite_t * image, const char * id, const char * rom_path, const char * product_code) {
    memset(title->id, 0, sizeof(title->id));
    memset(title->rom_path, 0, sizeof(title->rom_path));
    memset(title->product_code, 0, sizeof(title->product_code));
    strncpy(title->id, id, sizeof(title->id) - 1);
    if(rom_path != NULL) {
        strncpy(title->rom_path, rom_path, sizeof(title->rom_path) - 1);
    }
    if(product_code != NULL) {
        strncpy(title->product_code, product_code, sizeof(title->product_code) - 1);
    }
    title->image = image;
    title->sprite.x = 0.0f;
    title->sprite.y = 0.0f;
    title->sprite.width = TITLE_BOX_WIDTH_E;
    title->sprite.height = TITLE_BOX_HEIGHT_E;
    title->scale = 1.0f;
    title->offset_x = title->offset_y = 0.0f;
    title->isSelected = 0;
    title->scaleGrow = 0.0f;
    title->selectedOutline = 0.0f;
    title->outlineCounter = 0.0f;
}

void TitleBox_update(TitleBox * title) {
    if(title->isSelected) {
        title->scaleGrow += 1.0f / 10.0f;
        if(title->scaleGrow > 1.0f) title->scaleGrow = 1.0f;
        //title->selectedOutline += 1.0f/10.0f;

        title->selectedOutline = 1.0f - (cosf(title->outlineCounter) * 0.5f + 0.5f);
        //if(title->selectedOutline > 1.0f) title->selectedOutline = 1.0f;
        
        title->outlineCounter += 0.15f;
    } else {
        title->scaleGrow = 0.0f;
        title->selectedOutline = 0.0f;
        title->outlineCounter = 0.0f;
    }
}

void TitleBox_draw(TitleBox * title) {
    float titleScale = title->scale;
    float originalWidth = title->sprite.width * title->scale;
    float originalHeight = title->sprite.height * title->scale;
    if(title->isSelected) {
        float scaleHeight = (originalHeight + 16) / originalHeight;
        float scaleFactor = titleScale * scaleHeight;
        titleScale = titleScale + title->scaleGrow * (scaleFactor - titleScale);
    }

    // Center scale
    
    float scaledWidth = title->sprite.width * titleScale;
    float scaledHeight = title->sprite.height * titleScale;
    float scaledOffsetX = scaledWidth - originalWidth;
    float scaledOffsetY = scaledHeight - originalHeight;

    float offsetX = title->sprite.x - scaledOffsetX * 0.5f;
    float offsetY = title->sprite.y - scaledOffsetY * 0.5f;
    if(offsetX < box_region_min_x) offsetX = box_region_min_x;
    if((offsetX + scaledWidth) >= box_region_max_x) offsetX = box_region_max_x - scaledWidth;

    if(offsetY < box_region_min_y) offsetY = box_region_min_y;
    if((offsetY + scaledHeight) >= box_region_max_y) offsetY = box_region_max_y - scaledHeight;

    if((offsetY < viewport_y && (offsetY + scaledHeight) <= viewport_y) ||
    (offsetY >= viewport_y_bottom && (offsetY + scaledHeight) > viewport_y_bottom)) {
        return;
    }

    sprite_t *image = title->image != NULL ? title->image : get_boxart_sprite(title->id);
    if(image == NULL) {
        return;
    }

    if(title->isSelected) {
        float shadow_scaleX = titleScale * 1.1f;  
        float shadow_scaleY = titleScale * 1.05f; 
        float shadowSizeX = scaledWidth * shadow_scaleX;
        float shadowSizeY = scaledHeight * shadow_scaleY;
        float shadowOffsetX = offsetX - (shadowSizeX - scaledWidth) * 0.5f;
        
        shadow_draw(shadowOffsetX, offsetY - (viewport_y - BOX_REGION_Y_MIN), shadowSizeX, shadowSizeY, 0.23f);
    }
    int b = title_brightness * 255.0f;
    float imageScaleX = titleScale;
    float imageScaleY = titleScale;

    if(image->width > 0 && image->height > 0) {
        imageScaleX *= title->sprite.width / (float)image->width;
        imageScaleY *= title->sprite.height / (float)image->height;
    }

    rdpq_set_prim_color(RGBA32(b, b, b, 255));
    rdpq_sprite_blit(image, offsetX, offsetY - (viewport_y - BOX_REGION_Y_MIN), &(rdpq_blitparms_t){
        .scale_x = imageScaleX, .scale_y = imageScaleY,
    });

    if(title->isSelected) {
        outline_draw(
            offsetX, offsetY - (viewport_y - BOX_REGION_Y_MIN),
            title->sprite.width * titleScale, title->sprite.height * titleScale,
            title->selectedOutline
        );
    }
}

typedef struct MenuSidebar_s {
    SquareSprite sprite;
    float vertex[8];
    float width;
    int isOpen;
    int animCounter;
    int prevIsOpen;
} MenuSidebar;
MenuSidebar menu_sidebar;

void vertex_set_from_xywh(SquareSprite * sprite, float * out) {
    out[0] = sprite->x;
    out[1] = sprite->y;

    out[2] = sprite->x + sprite->width;
    out[3] = sprite->y + sprite->height;

    out[4] = out[0];
    out[5] = out[3];

    out[6] = out[2];
    out[7] = out[1];
}

int menu_sidebar_anim[] = {0, 10, 20, 30, 30, 20, 10, 0};

void menu_sidebar_update(MenuSidebar * obj) {
    vertex_set_from_xywh(&obj->sprite, obj->vertex);
    obj->prevIsOpen = obj->isOpen;
    if(cursor_x < 0) {
        obj->isOpen = 1;
    } else {
        obj->isOpen = 0;
    }

    if(obj->prevIsOpen != obj->isOpen) {
        if(obj->isOpen) {
            wav64_play_optional(&se_filemenu_open, se_filemenu_open_loaded, CHANNEL_SFX1);
        } else {
            wav64_play_optional(&se_filemenu_close, se_filemenu_close_loaded, CHANNEL_SFX1);
        }
    }

    if(obj->isOpen) {
        obj->animCounter++;
    } else {
        obj->animCounter--;
    }
    #define SIDE_MENU_OPN_FRM 10.0f

    if(obj->animCounter > SIDE_MENU_OPN_FRM) obj->animCounter = SIDE_MENU_OPN_FRM;
    if(obj->animCounter < 0) obj->animCounter = 0;

    float anmfact = obj->animCounter / SIDE_MENU_OPN_FRM;
    float interpl;
    
    if(obj->isOpen) {
        interpl = bezier_interp(anmfact, .44f,.88f,.57f,1.26f);
    } else {
        interpl = 1.0f - bezier_interp(1.0f - anmfact, .44f,.88f,.57f,1.15f);
    }
    obj->sprite.width = 64.0f + fabsf(interpl * 256.0f);
    title_brightness = 0.5f + (1.0f - anmfact) * 0.5f;





}
void menu_sidebar_draw(MenuSidebar * obj) {
    rdpq_set_prim_color(RGBA32(255, 0, 0, 0));
    rdpq_triangle(&TRIFMT_FILL, &obj->vertex[0], &obj->vertex[2], &obj->vertex[4]);
    rdpq_triangle(&TRIFMT_FILL, &obj->vertex[0], &obj->vertex[6], &obj->vertex[2]);
}

static uint64_t fnv1a64_update(uint64_t hash, const void *data, size_t len) {
    const uint8_t *bytes = data;

    for(size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }

    return hash;
}

static bool has_z64_extension(const char *path) {
    const char *ext = strrchr(path, '.');
    return (ext != NULL) && (strcmp(ext, ".z64") == 0);
}

static int compare_rom_scan_entries(const void *lhs, const void *rhs) {
    const RomScanEntry *a = lhs;
    const RomScanEntry *b = rhs;
    return strcmp(a->rom_path, b->rom_path);
}

static bool normalize_rom_header(uint8_t *header, size_t size) {
    if(size < 0x40) {
        return false;
    }

    switch(header[0]) {
        case 0x80:
            return true;
        case 0x37:
            for(size_t i = 0; i + 1 < size; i += 2) {
                uint8_t tmp = header[i];
                header[i] = header[i + 1];
                header[i + 1] = tmp;
            }
            return true;
        case 0x40:
            for(size_t i = 0; i + 3 < size; i += 4) {
                uint8_t tmp0 = header[i];
                uint8_t tmp1 = header[i + 1];
                header[i] = header[i + 3];
                header[i + 1] = header[i + 2];
                header[i + 2] = tmp1;
                header[i + 3] = tmp0;
            }
            return true;
        default:
            return false;
    }
}

static bool extract_product_code(const char *rom_path, char product_code[5]) {
    FILE *rom = fopen(rom_path, "rb");
    uint8_t header[0x40];

    if(rom == NULL) {
        return false;
    }
    if(fread(header, 1, sizeof(header), rom) != sizeof(header)) {
        fclose(rom);
        return false;
    }
    fclose(rom);

    if(!normalize_rom_header(header, sizeof(header))) {
        return false;
    }

    memcpy(product_code, &header[0x3B], 4);
    product_code[4] = 0;

    for(int i = 0; i < 4; i++) {
        if(product_code[i] < 0x20 || product_code[i] > 0x7E) {
            return false;
        }
    }

    return true;
}

static void derive_art_id(const char product_code[5], char art_id[3]) {
    art_id[0] = product_code[1];
    art_id[1] = product_code[2];
    art_id[2] = 0;
}

static void boxart_cache_reset(void) {
    for(int i = 0; i < BOXART_CACHE_SIZE; i++) {
        if(boxart_cache[i].sprite != NULL && boxart_cache[i].sprite != placeholder_n64) {
            sprite_free(boxart_cache[i].sprite);
        }
    }

    memset(boxart_cache, 0, sizeof(boxart_cache));
    boxart_cache_clock = 0;
}

static sprite_t *get_boxart_sprite(const char *art_id) {
    char path[64];
    sprite_t *sprite;
    int slot = -1;
    int lru_slot = 0;
    uint32_t lru_used = UINT32_MAX;

    if(art_id == NULL || strlen(art_id) != 2) {
        return placeholder_n64;
    }

    boxart_cache_clock++;
    if(boxart_cache_clock == 0) {
        boxart_cache_clock = 1;
    }

    for(int i = 0; i < BOXART_CACHE_SIZE; i++) {
        if(boxart_cache[i].sprite != NULL && strcmp(boxart_cache[i].art_id, art_id) == 0) {
            boxart_cache[i].last_used = boxart_cache_clock;
            return boxart_cache[i].sprite;
        }

        if(boxart_cache[i].sprite == NULL && slot < 0) {
            slot = i;
        }
        if(boxart_cache[i].last_used < lru_used) {
            lru_used = boxart_cache[i].last_used;
            lru_slot = i;
        }
    }

    if(slot < 0) {
        slot = lru_slot;
        if(boxart_cache[slot].sprite != NULL && boxart_cache[slot].sprite != placeholder_n64) {
            sprite_free(boxart_cache[slot].sprite);
        }
    }

    memset(&boxart_cache[slot], 0, sizeof(boxart_cache[slot]));
    snprintf(path, sizeof(path), "rom:/boxart/%s.sprite", art_id);
    sprite = sprite_load_optional(path);
    if(sprite == NULL) {
        sprite = placeholder_n64;
    }

    boxart_cache[slot].art_id[0] = art_id[0];
    boxart_cache[slot].art_id[1] = art_id[1];
    boxart_cache[slot].art_id[2] = 0;
    boxart_cache[slot].sprite = sprite;
    boxart_cache[slot].last_used = boxart_cache_clock;
    return sprite;
}

static void reset_titles(void) {
    boxart_cache_reset();
    title_count = 0;
    selectedTitle = NULL;
    cursor_x = 0;
    cursor_y = 0;
    cursor_x_timer = 0;
    cursor_y_timer = 0;
    viewport_y = BOX_REGION_Y_MIN;
    viewport_y_bottom = BOX_REGION_Y_MAX;
    viewport_y_target = BOX_REGION_Y_MIN;
    box_region_max_y = BOX_REGION_Y_MAX;
    memset(title_row, 0, sizeof(title_row));
    memset(title_row_count, 0, sizeof(title_row_count));
}

static void layout_titles(void) {
    float currentRowY = box_region_min_y;

    for(int y = 0; y < TITLE_ROWS; y++) {
        int currentRowCount = title_row_count[y];
        if(currentRowCount == 0) {
            continue;
        }

        float rowScale = 1.0f;
        if(currentRowCount > 2) {
            rowScale = (BOX_REGION_X_MAX - BOX_REGION_X_MIN)  / (currentRowCount * TITLE_BOX_WIDTH_E);
        }
        float scaledTitleX = TITLE_BOX_WIDTH_E * rowScale;

        for(int x = 0; x < TITLE_COLUMNS; x++) {
            TitleBox *current = title_row[y][x];
            if(current == NULL) {
                continue;
            }
            current->scale = rowScale;
            current->sprite.x = box_region_min_x + scaledTitleX * x;
            current->sprite.y = currentRowY;
        }

        currentRowY += TITLE_BOX_HEIGHT_E * rowScale;
    }

    if(currentRowY > SCREEN_HEIGHT) {
        box_region_max_y = currentRowY;
    }
}

static void populate_titles_from_cache_entries(const RomCacheEntry *entries, int count) {
    reset_titles();

    for(int i = 0; i < count && i < MAX_TITLE_COUNT; i++) {
        int row = title_count / TITLE_COLUMNS;
        int col = title_count % TITLE_COLUMNS;

        TitleBox_create2(&title_list[title_count], NULL, entries[i].art_id, entries[i].rom_path, entries[i].product_code);
        title_row[row][col] = &title_list[title_count];
        title_count++;
    }

    for(int y = 0; y < TITLE_ROWS; y++) {
        for(int x = 0; x < TITLE_COLUMNS; x++) {
            if(title_row[y][x] != NULL) {
                title_row_count[y]++;
            }
        }
    }

    layout_titles();
}

static int scan_rom_directory(RomScanEntry *entries, bool read_headers, uint64_t *fingerprint_out) {
    dir_t dir = {0};
    int count = 0;
    int ret = dir_findfirst(ROMS_DIRECTORY, &dir);
    uint64_t fingerprint = 1469598103934665603ULL;
    size_t roms_dir_len = strlen(ROMS_DIRECTORY);

    if(ret < 0) {
        if(fingerprint_out != NULL) {
            *fingerprint_out = fingerprint;
        }
        return 0;
    }

    do {
        if(dir.d_type != DT_REG || !has_z64_extension(dir.d_name)) {
            continue;
        }
        if(count >= MAX_TITLE_COUNT) {
            break;
        }
        if(roms_dir_len + 1 + strlen(dir.d_name) >= sizeof(entries[count].rom_path)) {
            continue;
        }

        snprintf(entries[count].rom_path, sizeof(entries[count].rom_path), "%s/%s", ROMS_DIRECTORY, dir.d_name);
        entries[count].file_size = dir.d_size;
        entries[count].product_code[0] = 0;
        entries[count].art_id[0] = 0;
        count++;
    } while(dir_findnext(ROMS_DIRECTORY, &dir) == 0);

    qsort(entries, count, sizeof(entries[0]), compare_rom_scan_entries);

    for(int i = 0; i < count; i++) {
        const char *basename = strrchr(entries[i].rom_path, '/');
        uint64_t file_size = (uint64_t)((entries[i].file_size < 0) ? 0 : entries[i].file_size);

        basename = (basename != NULL) ? basename + 1 : entries[i].rom_path;
        fingerprint = fnv1a64_update(fingerprint, basename, strlen(basename) + 1);
        fingerprint = fnv1a64_update(fingerprint, &file_size, sizeof(file_size));

        if(read_headers) {
            if(!extract_product_code(entries[i].rom_path, entries[i].product_code)) {
                entries[i].rom_path[0] = 0;
                continue;
            }
            derive_art_id(entries[i].product_code, entries[i].art_id);
        }
    }

    if(fingerprint_out != NULL) {
        *fingerprint_out = fingerprint;
    }

    return count;
}

static bool load_cache_entries(uint64_t fingerprint, RomCacheEntry *entries, int *count_out) {
    FILE *cache = fopen(ROM_CACHE_PATH, "rb");
    RomCacheHeader header;

    if(cache == NULL) {
        return false;
    }
    if(fread(&header, 1, sizeof(header), cache) != sizeof(header)) {
        fclose(cache);
        return false;
    }
    if(header.magic != ROM_CACHE_MAGIC || header.version != ROM_CACHE_VERSION || header.fingerprint != fingerprint) {
        fclose(cache);
        return false;
    }
    if(header.entry_count > MAX_TITLE_COUNT) {
        fclose(cache);
        return false;
    }
    if(header.entry_count > 0 && fread(entries, sizeof(entries[0]), header.entry_count, cache) != header.entry_count) {
        fclose(cache);
        return false;
    }
    fclose(cache);

    for(uint32_t i = 0; i < header.entry_count; i++) {
        if(entries[i].rom_path[sizeof(entries[i].rom_path) - 1] != 0 ||
           entries[i].product_code[sizeof(entries[i].product_code) - 1] != 0 ||
           entries[i].art_id[sizeof(entries[i].art_id) - 1] != 0 ||
           strncmp(entries[i].rom_path, ROMS_DIRECTORY "/", strlen(ROMS_DIRECTORY) + 1) != 0 ||
           strlen(entries[i].product_code) != 4 ||
           strlen(entries[i].art_id) != 2) {
            return false;
        }
    }

    *count_out = (int)header.entry_count;
    return true;
}

static void write_cache_entries(const RomCacheEntry *entries, int count, uint64_t fingerprint) {
    FILE *cache;
    RomCacheHeader header = {
        .magic = ROM_CACHE_MAGIC,
        .version = ROM_CACHE_VERSION,
        .fingerprint = fingerprint,
        .entry_count = (uint32_t)count,
    };

    if(directory_create("sd:/menu")) {
        return;
    }

    cache = fopen(ROM_CACHE_TMP_PATH, "wb");
    if(cache == NULL) {
        return;
    }
    if(fwrite(&header, 1, sizeof(header), cache) != sizeof(header)) {
        fclose(cache);
        file_delete(ROM_CACHE_TMP_PATH);
        return;
    }
    if(count > 0 && fwrite(entries, sizeof(entries[0]), count, cache) != (size_t)count) {
        fclose(cache);
        file_delete(ROM_CACHE_TMP_PATH);
        return;
    }
    fclose(cache);

    file_delete(ROM_CACHE_PATH);
    rename(ROM_CACHE_TMP_PATH, ROM_CACHE_PATH);
}

static bool load_titles_from_cache(void) {
    uint64_t fingerprint;
    int entry_count;

    scan_rom_directory(rom_scan_entries, false, &fingerprint);
    if(!load_cache_entries(fingerprint, rom_cache_entries, &entry_count)) {
        return false;
    }

    populate_titles_from_cache_entries(rom_cache_entries, entry_count);
    return true;
}

static void rebuild_titles_from_sd(void) {
    uint64_t fingerprint;
    int scan_count = scan_rom_directory(rom_scan_entries, true, &fingerprint);
    int cache_count = 0;

    for(int i = 0; i < scan_count && cache_count < MAX_TITLE_COUNT; i++) {
        if(rom_scan_entries[i].rom_path[0] == 0 || rom_scan_entries[i].product_code[0] == 0) {
            continue;
        }

        memset(&rom_cache_entries[cache_count], 0, sizeof(rom_cache_entries[cache_count]));
        memcpy(rom_cache_entries[cache_count].rom_path, rom_scan_entries[i].rom_path, sizeof(rom_cache_entries[cache_count].rom_path));
        memcpy(rom_cache_entries[cache_count].product_code, rom_scan_entries[i].product_code, sizeof(rom_cache_entries[cache_count].product_code));
        memcpy(rom_cache_entries[cache_count].art_id, rom_scan_entries[i].art_id, sizeof(rom_cache_entries[cache_count].art_id));
        cache_count++;
    }

    write_cache_entries(rom_cache_entries, cache_count, fingerprint);
    populate_titles_from_cache_entries(rom_cache_entries, cache_count);
}

void setupRomLoad(TitleBox * title) {
    strncpy(rom_path, title->rom_path, sizeof(rom_path) - 1);
    rom_path[sizeof(rom_path) - 1] = 0;

    flashcart_load_rom(rom_path, false, cart_load_progress);

    //boot_params.reset_type = BOOT_RESET_TYPE_NMI;
    boot_params.device_type = BOOT_DEVICE_TYPE_ROM;
    boot_params.tv_type = BOOT_TV_TYPE_PASSTHROUGH;
    boot_params.detect_cic_seed = true;

     menu_active = false;
}

float gametitle_fade = 0.0f;
SquareSprite gametitle_fade_sprite;
float gametitle_fade_vertex[8];

void menu_update() {

    menu_sidebar_update(&menu_sidebar);

    if(title_count == 0) {
        selectedTitle = NULL;
        viewport_y_target = box_region_min_y;
        viewport_y = box_region_min_y;
        viewport_y_bottom = BOX_REGION_Y_MAX;
        return;
    }

    int prev_cursor_x = cursor_x;
    int prev_cursor_y = cursor_y;

    int limit_reached = 0;
    int cursor_move = 0;

    int currentRowCount = title_row_count[cursor_y];

    if(main_state == 3) {
        int input_x = 0;
        int input_y = 0;
        
        if(p1_buttons.d_left) {
            input_x = -1;
        }
        if(p1_buttons.d_right) {
            input_x = 1;
        }
        if(p1_inputs.stick_x < -30 || p1_inputs.stick_x > 30) {
            input_x = p1_inputs.stick_x;
        }

        if(p1_buttons.d_up) {
            input_y = -1;
        }
        if(p1_buttons.d_down) {
            input_y = 1;
        }
        if(p1_inputs.stick_y < -30 || p1_inputs.stick_y > 30) {
            input_y = -p1_inputs.stick_y;
        }
        
        if(input_x < 0) {
            if(cursor_x_timer == 0){ 
                cursor_x--;
            }
            cursor_x_timer++;
            if(cursor_x_timer > CURSOR_INTERVAL) {
                cursor_x--;
                cursor_x_timer = CURSOR_INTERVAL_SECOND;
            }
        } else if(input_x > 0) {
            if(cursor_x_timer == 0){ 
                cursor_x++;
            }
            cursor_x_timer++;
            if(cursor_x_timer > CURSOR_INTERVAL) {
                cursor_x++;
                cursor_x_timer = CURSOR_INTERVAL_SECOND;
            }
        } else {
            cursor_x_timer = 0;
        }
        if(cursor_x >= 0) {
            if(input_y < 0) {
                if(cursor_y_timer == 0){ 
                    cursor_y--;
                }
                cursor_y_timer++;
                if(cursor_y_timer > CURSOR_INTERVAL) {
                    cursor_y--;
                    cursor_y_timer = CURSOR_INTERVAL_SECOND;
                }
            } else if(input_y > 0) {
                if(cursor_y_timer == 0){ 
                    cursor_y++;
                }
                cursor_y_timer++;
                if(cursor_y_timer > CURSOR_INTERVAL) {
                    cursor_y++;
                    cursor_y_timer = CURSOR_INTERVAL_SECOND;
                }
            } else {
                cursor_y_timer = 0;
            }
        }
    }

    if(cursor_x >= 0) {
        if(cursor_y < 0) {
            if(prev_cursor_y != cursor_y) limit_reached = 1;
            cursor_y = 0;
        } else if (cursor_y >= TITLE_ROWS) {
            if(prev_cursor_y != cursor_y) limit_reached = 1;
            cursor_y = TITLE_ROWS-1;
        }

        // Next row?
        int rowCount = title_row_count[cursor_y];
        if(rowCount == 0) {
            cursor_y--;
            limit_reached = 1;
            rowCount = title_row_count[cursor_y];
        } else {
            if(prev_cursor_y != cursor_y) cursor_move = 1;
        }

    
        // Select most fitting x based on title sizes
        if(rowCount > 1) {
            float nextFitting = ((float)cursor_x + 0.5f) / (float)currentRowCount;
            cursor_x = nextFitting * rowCount;
        }
        
        if(cursor_x < 0) {
            if(prev_cursor_x != cursor_x) {
                limit_reached = 1;
            }
            cursor_x = 0;

        } else if(cursor_x >= rowCount) {
            if(prev_cursor_x != cursor_x) {
                limit_reached = 1;
            }
            cursor_x = rowCount - 1;
        } else {
            if(prev_cursor_x != cursor_x) {
                cursor_move = 1;
            }
            
        }
    } else {
        // Lateral menu activated
        if(cursor_x < -1) {
            cursor_x = -1;
        }
    }
    

    if(limit_reached) {
        wav64_play_optional(&se_cursor_ng, se_cursor_ng_loaded, CHANNEL_SFX1);
    }
    if(cursor_move) {
        wav64_play_optional(&se_gametitle_cursor, se_gametitle_cursor_loaded, CHANNEL_SFX1);
    }

    if(cursor_x >= 0) {
        TitleBox * cur = title_row[cursor_y][cursor_x];
        if(cur != NULL) {
            if(selectedTitle != NULL) {
                selectedTitle->isSelected = 0;
            }
            cur->isSelected = 1;
            selectedTitle = cur;
        }
    } else {
        if(selectedTitle != NULL) {
            selectedTitle->isSelected = 0;
        }
        selectedTitle = NULL;
    }

    if(cursor_x >= 0 && selectedTitle && main_state == 3) {
        if(p1_buttons_press.a) {
            main_state = 4;
            selectedTitle->scaleGrow = 0.0f;
            wav64_play_optional(&se_gametitle_click, se_gametitle_click_loaded, CHANNEL_SFX1);
        }
    }

    

    for(int y = 0; y < TITLE_ROWS; y++) {
        for(int x = 0; x < TITLE_COLUMNS; x++) {
            TitleBox * current = title_row[y][x];
            if(current == NULL) continue;
            TitleBox_update(current);
        }
    }

    if(selectedTitle) {
        float viewMidpoint = 240.0f;
        float midpointY = selectedTitle->sprite.y + (selectedTitle->sprite.height * 0.5);
        viewport_y_target = midpointY - viewMidpoint;

        if(viewport_y_target < box_region_min_y) viewport_y_target = box_region_min_y;
        if((viewport_y_target + (BOX_REGION_Y_MAX - BOX_REGION_Y_MIN)) > box_region_max_y) viewport_y_target = box_region_max_y - (BOX_REGION_Y_MAX - BOX_REGION_Y_MIN);
        viewport_y_target = roundf(viewport_y_target);
    }
    
    float vydiff = viewport_y_target - viewport_y;
    if(fabsf(vydiff) > 0.1f) {
        viewport_y += (viewport_y_target - viewport_y) * 0.4f;
    } else {
        viewport_y = viewport_y_target;
    }

    viewport_y_bottom = viewport_y + (BOX_REGION_Y_MAX - BOX_REGION_Y_MIN);

    // fade out
    if(main_state > 3) {
        switch(main_state) {
            case 4:
                gametitle_fade += 1.0f / 15.0f;
                if(gametitle_fade >= 1.0f) {
                    gametitle_fade = 1.0f;
                    main_state = 5;
                }
                break;
            case 5:
                setupRomLoad(selectedTitle);
                if(p1_buttons_press.b) {
                    main_state = 6;
                    spinner_fade = 0.0f;
                    spinner_fade_counter = 0;
                    spinner_counter = 0.0f;
                }
                break;
            case 6:
                gametitle_fade -= 1.0f / 20.0f;
                if(gametitle_fade <= 0.0f) {
                    gametitle_fade = 0.0f;
                    main_state = 3;
                }
                break;
        }
        float midpointX = selectedTitle->sprite.x + (selectedTitle->sprite.width * selectedTitle->scale) * 0.5;
        float midpointY = selectedTitle->sprite.y + (selectedTitle->sprite.height * selectedTitle->scale) * 0.5;
        midpointY -= (viewport_y - BOX_REGION_Y_MIN);
        gametitle_fade_sprite.x = lerp(gametitle_fade, midpointX - 32.0f, 0.0f);
        gametitle_fade_sprite.y = lerp(gametitle_fade, midpointY - 32.0f, 0.0f);
        gametitle_fade_sprite.width = lerp(gametitle_fade, 64.0f, 640.0f);
        gametitle_fade_sprite.height = lerp(gametitle_fade, 64.0f, 480.0f);
        vertex_set_from_xywh(&gametitle_fade_sprite, gametitle_fade_vertex);
    }


}
void menu_draw() {
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);    
    
    if(main_state != 5) {
        int minSciss = opening_card_offset_x < menu_sidebar.sprite.width ? opening_card_offset_x : menu_sidebar.sprite.width;
        rdpq_set_scissor(minSciss,BOX_REGION_Y_MIN,BOX_REGION_X_MAX,BOX_REGION_Y_MAX);

        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        
        rdpq_mode_filter(FILTER_BILINEAR);

        for(int y = 0; y < TITLE_ROWS; y++) {
            for(int x = 0; x < TITLE_COLUMNS; x++) {
                TitleBox * current = title_row[y][x];
                if(current == NULL || current == selectedTitle) continue;
                TitleBox_draw(current);
            }
        }
        if(selectedTitle != NULL) {
            // Draw last
            TitleBox_draw(selectedTitle);
        }

        rdpq_set_scissor(BOX_REGION_Y_MIN,BOX_REGION_Y_MIN,BOX_REGION_X_MAX,BOX_REGION_Y_MAX);

        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        
        menu_sidebar_draw(&menu_sidebar);

    }

    if(main_state > 3) {
        float gametitle_fade_c = gametitle_fade * 255.0f;
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, gametitle_fade_c));
        rdpq_triangle(&TRIFMT_FILL, &gametitle_fade_vertex[0], &gametitle_fade_vertex[2], &gametitle_fade_vertex[4]);
        rdpq_triangle(&TRIFMT_FILL, &gametitle_fade_vertex[0], &gametitle_fade_vertex[6], &gametitle_fade_vertex[2]);
        if(main_state == 5) {
            spinner_fade_counter++;
            if(spinner_fade_counter >= 10) {
                spinner_fade = (spinner_fade_counter - 10) / 30.0f;
                if(spinner_fade > 1.0f) spinner_fade = 1.0f;

                spinner_draw(640 - 55, 480 - 48, 20, spinner_fade);
            }
        }
    }
}

static void draw_loading_splash(void) {
    int alpha = loading_splash_alpha * 255.0f;

    if(alpha <= 0) {
        return;
    }

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));
    rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    if(loading_splash != NULL) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_ALPHA);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(255, 255, 255, alpha));
        rdpq_sprite_blit(loading_splash, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, &(rdpq_blitparms_t){
            .cx = loading_splash->width / 2,
            .cy = loading_splash->height / 2,
        });
    }

    spinner_draw(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 20, loading_splash_alpha);
}

static void draw_boot_splash(void) {
    if(loading_splash == NULL) {
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 0));
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        return;
    }

    rdpq_set_mode_fill(RGBA32(0, 0, 0, 0));
    rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_ALPHA);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_sprite_blit(loading_splash, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, &(rdpq_blitparms_t){
        .cx = loading_splash->width / 2,
        .cy = loading_splash->height / 2,
    });
}

void update(int ovfl) {
    switch(fade_state) {
        case 0:
        fade_lvl = fade_counter / FADE_DURATION;
        fade_counter--;
        if(fade_counter == 0) fade_state = 1;
        break;
        default:
        break;
    }

    if(fade_state == 1 && main_state == 0) {
        main_state = 1;
    }
    switch(main_state) {
        default: case 0: break; // Wait Fade
        case 1: // Wait logo
            opening_counter--;
            if(opening_counter == OPENING_SOUND_PLAY) {
                
                wav64_play_optional(&se_titlelogo, se_titlelogo_loaded, CHANNEL_SFX1);
            }
            if(opening_counter == 0) {
                main_state = 2;
                opening_counter = OPENING_OUT;
            }
        break;
        case 2: // exit logo out of the screen
            opening_card_offset_x = ((1.0f - opening_counter / OPENING_OUT) * 640.0f);
            opening_counter--;
            if(opening_counter == 0) {
                if(logo != NULL) {
                    sprite_free(logo);
                    logo = NULL;
                }
                if(load_titles_from_cache()) {
                    main_state = 3;
                } else {
                    loading_splash_alpha = 0.0f;
                    main_state = 7;
                }
            }
            break;
        case 7:
            loading_splash_alpha += 1.0f / 15.0f;
            if(loading_splash_alpha >= 1.0f) {
                loading_splash_alpha = 1.0f;
                main_state = 8;
            }
            break;
        case 8:
            rebuild_titles_from_sd();
            main_state = 9;
            break;
        case 9:
            loading_splash_alpha -= 1.0f / 15.0f;
            if(loading_splash_alpha <= 0.0f) {
                loading_splash_alpha = 0.0f;
                main_state = 3;
            }
            break;
    }

    if(main_state >= 2 && main_state != 7 && main_state != 8 && main_state != 9) menu_update();
}

void draw(int cur_frame) {
    surface_t *disp = display_get();
    rdpq_attach(disp, NULL);

    rdpq_set_scissor(SCR_REGION_X_MIN,BOX_REGION_Y_MIN,BOX_REGION_X_MAX,BOX_REGION_Y_MAX);

    if(main_state >= 2) {
        rdpq_set_mode_fill(RGBA32(48,48,48,0));
        rdpq_fill_rectangle(64, BOX_REGION_Y_MIN, BOX_REGION_X_MAX, BOX_REGION_Y_MAX);
        if(main_state != 7 && main_state != 8) {
            menu_draw();
        }
    }

    if(main_state < 3) { 
        draw_boot_splash();

        rdpq_set_mode_copy(true);

        if(logo != NULL) {
            rdpq_sprite_blit(logo, 194 - opening_card_offset_x, 112, &(rdpq_blitparms_t){
                .scale_x = 1, .scale_y = 1,
            });
        }
    }

     // Draw Fade
    if(fade_state != 1) {
        rdpq_set_mode_standard();
        rdpq_mode_dithering(DITHER_NOISE_NOISE);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, fade_lvl * 255));
        rdpq_triangle(&TRIFMT_FILL, fade_v1, fade_v2, fade_v3);
        rdpq_triangle(&TRIFMT_FILL, fade_v1, fade_v4, fade_v2);
    }

    if(main_state == 7 || main_state == 8 || main_state == 9) {
        draw_loading_splash();
    }

    // For measuring performance
    /*float fps = display_get_fps();
    rdpq_text_printf(&(rdpq_textparms_t){
        .align = ALIGN_LEFT,
        .width = 400,
    }, 1, 32, 55, "FPS: %.4f", fps);*/

    rdpq_detach_show();
}

int main(void)
{
    /* Initialize peripherals */
    display_init(RESOLUTION_640x480, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_DEDITHER);
    dfs_init( DFS_DEFAULT_LOCATION );
    debug_init_sdfs("sd:/", -1);
    rdpq_init();
    joypad_init();
    timer_init();

    audio_init(44100, 4);
	mixer_init(20);

    se_titlelogo_loaded = wav64_open_optional(&se_titlelogo, "rom:/se/titlelogo.wav64");
    se_cursor_ng_loaded = wav64_open_optional(&se_cursor_ng, "rom:/se/cursor_ng.wav64");
    se_gametitle_cursor_loaded = wav64_open_optional(&se_gametitle_cursor, "rom:/se/gametitle_cursor.wav64");
    se_filemenu_close_loaded = wav64_open_optional(&se_filemenu_close, "rom:/se/filemenu_close.wav64");
    se_filemenu_open_loaded = wav64_open_optional(&se_filemenu_open, "rom:/se/filemenu_open.wav64");
    se_gametitle_click_loaded = wav64_open_optional(&se_gametitle_click, "rom:/se/gametitle_click.wav64");
    
    shadow = sprite_load("rom:/shadow.sprite");
    loading_dot = sprite_load("rom:/loading_dot.sprite");
    placeholder_n64 = sprite_load("rom:/ui/placeholder-n64.sprite");
    loading_splash = sprite_load("rom:/ui/splash.sprite");
    logo = sprite_load_optional("rom:/logo.sprite");
    font = rdpq_font_load("rom:/default.font64");
    rdpq_text_register_font(1, font);
    

    menu_sidebar.sprite.x = 0;
    menu_sidebar.sprite.y = 0;
    menu_sidebar.sprite.width = 64;
    menu_sidebar.sprite.height = 480;
    menu_sidebar.width = 64.0f;
    menu_sidebar.isOpen = 0;
    menu_sidebar.animCounter = 0;

    flashcart_init();
    
    int cur_frame = 0;
    menu_active = 1;
    while(menu_active) {
        update(0);
        draw(cur_frame);
        joypad_poll();
        p1_buttons = joypad_get_buttons(JOYPAD_PORT_1);
        p1_buttons_press = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        p1_inputs = joypad_get_inputs(JOYPAD_PORT_1);
        if(audio_can_write()) {
            short *audio_buffer = audio_write_begin();
            mixer_poll(audio_buffer, audio_get_buffer_length());
            audio_write_end();
        }
        cur_frame++;
    }

    flashcart_deinit();


	mixer_close();
    audio_close();

    rdpq_close();
    rspq_close();
    timer_close();
    joypad_close();

    display_close();

    disable_interrupts();

    boot(&boot_params);

    assertf(false, "Unexpected return from 'boot' function");

    while (true) {

    }
}

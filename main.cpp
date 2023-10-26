#define GL_GLEXT_PROTOTYPES
#include <iostream>
#include <map>
#include <string>
#include <link.h>
#include <dlfcn.h>
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <ft2build.h>
#include <filesystem>
#include <vector>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "sdk.h"

struct grafic_character {
    unsigned int texture_id;
    glm::ivec2   size;
    glm::ivec2   bearing;
    unsigned int advance;
};

struct mmaped_elf_node
{
    struct mmaped_elf_node* next;
    void* address;
    char path[4096];
};

struct mmaped_elf_node* map_list_shared;

struct resolve_sym_info
{
    char name_shared_object[200];
    char symbol[200];
    void* address_symbol;
};

struct hook_relocation_info
{
    char name_shared_object[200];
    char symbol_name[200];
    void* prev_address;
    void* new_address;
};

unsigned int VAO, VBO;
int width, height;
int screen_center_x = 0;
int screen_center_y = 0;

SDL_GLContext original_context = nullptr;
SDL_GLContext user_context = nullptr;

vector<entity>* ents = nullptr;

void (*addmsg) (int type, const char *fmt, ...) = nullptr;

vector<playerent*>* players = nullptr;
glmatrixf* modelviewproj = nullptr;
glmatrixf* projmatrix = nullptr;

typedef int          (*sdl_poll_event_ptr)      (SDL_Event * event);
typedef float          (*sdl_sinf_ptr)        (float value);
typedef void           (*sdl_swap_buffer_ptr) (SDL_Window* window);
typedef void           (*sdl_make_current_ptr)    (SDL_Window * window, SDL_GLContext context);
typedef void           (*sdl_get_window_size_ptr) (SDL_Window* window, int* w, int* h);
typedef SDL_GLContext* (*sdl_create_contex_ptr)   (SDL_Window* window);
typedef SDL_GLContext* (*sdl_get_current_ptr)     (SDL_Window* window);
typedef void  (*shoot) (playerent *p, vec &targ);

sdl_poll_event_ptr sdl_poll_event_original = nullptr;
sdl_sinf_ptr sdl_sinf_original = nullptr;
sdl_make_current_ptr sdl_make_current = nullptr;
sdl_get_window_size_ptr sdl_get_window_size = nullptr;
sdl_create_contex_ptr sdl_create_contex = nullptr;
sdl_get_current_ptr sdl_get_current = nullptr;
sdl_swap_buffer_ptr sdl_swap_buffer_original = nullptr;
shoot sendsht = nullptr;

float_t* fov = nullptr;
int64_t* player1_field = nullptr;
int64_t* world_field = nullptr;
int32_t* sfactor = nullptr;

const vec frend_color(0.0f, 1.0f, 0.0f);
const vec enemy_color(1.0f, 0.0f, 0.0f);

const char* flag_str = "flag";
const vec flag_color(1.0f, 1.0f, 1.0f);

const char* health_str = "health";
const vec health_color(1.0f, 1.0f, 0.0f);

const char* clips_str = "clips";
const vec clips_color(0.0f, 1.0f, 1.0f);

const char* ammo_str = "ammo";
const vec ammo_color(0.0f, 1.0f, 1.0f);

const char* grenade_str = "grenade";
const vec grenade_color(1.0, 0.0, 1.0);

const char* helmet_str = "helmet";
const vec helmet_color(1.0, 0.0, 1.0);

const char* armour_str = "armour";
const vec armour_color(1.0, 0.0, 1.0);

const char* akimbo_str = "akimbo";
const vec akimbo_color(1.0, 0.0, 1.0);

struct esp_config
{
    bool health;
    bool ammo;
    bool armour;
    bool clips;
    bool flag;
    bool akimbo;
    bool granade;
    bool helmet;
    bool player;
} esp_config;

struct aimbot_config
{
    bool shoot;
    int aimbot_fov;
} aimbot_config;

std::map<GLchar, grafic_character> characters_cache;

std::string vshader =
        "#version 330 core \n"
        "layout (location = 0) in vec4 vertex; // <vec2 pos, vec2 tex>\n"
        "out vec2 TexCoords; \n"
        "uniform mat4 projection; \n"

        "void main() \n"
        "{ \n"
            "gl_Position = projection * vec4(vertex.xy, 0.0, 1.0); \n"
            "TexCoords = vertex.zw; \n"
        "}";

std::string fshader =
        "#version 330 core \n"
        "in vec2 TexCoords; \n"
        "out vec4 color; \n"

        "uniform sampler2D text; \n"
        "uniform vec3 textColor; \n"

        "void main() \n"
        "{ \n"
            "vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r); \n"
            "color = vec4(textColor, 1.0) * sampled; \n"
        "}";

void swap_buffer_handler(SDL_Window* window);

int hook_global_offset_table(struct dl_phdr_info *info, size_t size, void *data);

struct mmaped_elf_node* search_map_file(const char* const path, struct mmaped_elf_node* begin);

int add_file_to_map_list(const char* const path, struct mmaped_elf_node** begin, void** map_elf);

int resolve_symtab_symbol(struct dl_phdr_info *info, size_t size, void *data);

bool world_to_screen(vec pos, vec &screen, float matrix[16], int windowWidth, int windowHeight);

int sdl_poll_event_handler(SDL_Event* event);

void render_text(GLint program, std::string& text, float x, float y, float scale, const vec* color);

GLint load_program_shader(std::string& vertex_shader_code, std::string& fragment_shader_code);

int load_font_ascii(FT_Face& face, std::map<GLchar, grafic_character>& cache);

char* get_path_exutable();

__attribute__((constructor)) void entry()
{
    struct hook_relocation_info swap_buffer_hook = {"linux_64_client",
                                                    "SDL_GL_SwapWindow",
                                                    NULL,
                                                    reinterpret_cast<void*>(swap_buffer_handler)};

    struct hook_relocation_info sdl_poll_event_hook = {"linux_64_client",
                                                    "SDL_PollEvent",
                                                    NULL,
                                                    reinterpret_cast<void*>(sdl_poll_event_handler)};

    struct resolve_sym_info camera_sym = {"linux_64_client", "mvpmatrix", NULL};
    struct resolve_sym_info projmatrix_sym = {"linux_64_client", "projmatrix", NULL};
    struct resolve_sym_info players_sym = {"linux_64_client", "players", NULL};
    struct resolve_sym_info player1_sym = {"linux_64_client", "player1", NULL};
    struct resolve_sym_info ents_sym = {"linux_64_client", "ents", NULL};
    struct resolve_sym_info world_sym = {"linux_64_client", "world", NULL};
    struct resolve_sym_info sfactor_sym = {"linux_64_client", "sfactor", NULL};
    struct resolve_sym_info sendshot_sym = {"linux_64_client", "_Z5shootP9playerentR3vec", NULL};
    struct resolve_sym_info fov_sym = {"linux_64_client", "fov", NULL};


    dl_iterate_phdr(resolve_symtab_symbol, &fov_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &sendshot_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &sfactor_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &world_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &camera_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &projmatrix_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &players_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &player1_sym);
    dl_iterate_phdr(resolve_symtab_symbol, &ents_sym);
    dl_iterate_phdr(hook_global_offset_table, &sdl_poll_event_hook);
    dl_iterate_phdr(hook_global_offset_table, &swap_buffer_hook);

    sdl_swap_buffer_original = reinterpret_cast<sdl_swap_buffer_ptr> (dlsym(RTLD_DEFAULT, "SDL_GL_SwapWindow"));
    sdl_get_current = reinterpret_cast<sdl_get_current_ptr> (dlsym(RTLD_DEFAULT, "SDL_GL_GetCurrentContext"));
    sdl_create_contex = reinterpret_cast<sdl_create_contex_ptr> (dlsym(RTLD_DEFAULT, "SDL_GL_CreateContext"));
    sdl_make_current = reinterpret_cast<sdl_make_current_ptr> (dlsym(RTLD_DEFAULT, "SDL_GL_MakeCurrent"));
    sdl_get_window_size = reinterpret_cast<sdl_get_window_size_ptr> (dlsym(RTLD_DEFAULT, "SDL_GetWindowSize"));
    sdl_sinf_original = reinterpret_cast<sdl_sinf_ptr> (dlsym(RTLD_DEFAULT, "SDL_sinf"));
    sdl_poll_event_original = reinterpret_cast<sdl_poll_event_ptr> (dlsym(RTLD_DEFAULT, "SDL_PollEvent"));

    players = reinterpret_cast<vector<playerent*>*>(players_sym.address_symbol);
    modelviewproj = reinterpret_cast<glmatrixf*>(camera_sym.address_symbol);
    player1_field = reinterpret_cast<int64_t*>(player1_sym.address_symbol);
    ents = reinterpret_cast<vector<entity>*>(ents_sym.address_symbol);
    projmatrix = reinterpret_cast<glmatrixf*>(projmatrix_sym.address_symbol);
    world_field = reinterpret_cast<int64_t*>(world_sym.address_symbol);
    sfactor = reinterpret_cast<int32_t*>(sfactor_sym.address_symbol);
    sendsht = reinterpret_cast<shoot>(sendshot_sym.address_symbol);
    fov = reinterpret_cast<float_t*>(fov_sym.address_symbol);

    assert(sendshot_sym.address_symbol != nullptr);
    assert(sfactor_sym.address_symbol != nullptr);
    assert(world_sym.address_symbol != nullptr);
    assert(projmatrix_sym.address_symbol != nullptr);
    assert(players_sym.address_symbol != nullptr);
    assert(camera_sym.address_symbol != nullptr);
    assert(player1_sym.address_symbol != nullptr);
    assert(swap_buffer_handler != nullptr);
    aimbot_config.aimbot_fov = 20;
}

int sdl_poll_event_handler(SDL_Event* event)
{
    playerent* local_player = reinterpret_cast<playerent*>(*player1_field);
    int ret = sdl_poll_event_original(event);

    if (event->key.keysym.sym == SDLK_1 && event->type == SDL_KEYUP)
        esp_config.health = !esp_config.health;

    else if (event->key.keysym.sym == SDLK_2 && event->type == SDL_KEYUP)
        esp_config.armour = !esp_config.armour;

    else if (event->key.keysym.sym == SDLK_3 && event->type == SDL_KEYUP)
        esp_config.helmet = !esp_config.helmet;

    else if (event->key.keysym.sym == SDLK_4 && event->type == SDL_KEYUP)
        esp_config.granade = !esp_config.granade;

    else if (event->key.keysym.sym == SDLK_5 && event->type == SDL_KEYUP)
        esp_config.akimbo = !esp_config.akimbo;

    else if (event->key.keysym.sym == SDLK_6 && event->type == SDL_KEYUP)
        esp_config.flag = !esp_config.flag;

    else if (event->key.keysym.sym == SDLK_7 && event->type == SDL_KEYUP)
        esp_config.player = !esp_config.player;

    else if (event->key.keysym.sym == SDLK_8 && event->type == SDL_KEYUP)
        esp_config.ammo = !esp_config.ammo;

    else if (event->key.keysym.sym == SDLK_9 && event->type == SDL_KEYUP)
            esp_config.clips = !esp_config.clips;

    if (local_player->state != CS_ALIVE)
        return ret;
    else if (event->key.keysym.sym == SDLK_r && event->type == SDL_KEYDOWN)
            local_player->spectatemode = SM_FLY;

    else if (event->key.keysym.sym == SDLK_r && event->type == SDL_KEYUP)
            local_player->spectatemode = SM_NONE;

    else if (event->key.keysym.sym == SDLK_f && event->type == SDL_KEYDOWN)
            aimbot_config.shoot = true;

    return ret;
}

float distance_vec(vec& player, vec& target)
{
    return sqrt(pow(target.x - player.x, 2) +
                pow(target.y - player.y, 2) +
                pow(target.z - player.z, 2));
}

__inline__ double fast_divide( double y, double x) {
    union {
        double dbl;
        unsigned long long ull;
    } u;

    u.dbl = x;
    u.ull = ( 0xbfcdd6a18f6a6f52ULL - u.ull ) >> (unsigned char)1;

    u.dbl *= u.dbl;
    return u.dbl * y;
}

double fast_sin(double x)
{
  const double A = 4.0 / (M_PI * M_PI);
  const double P =  0.2248391013559941;
  double y = A * x * ( M_PI - x );
  return y * (( 1 - P) + y * P );
}

void swap_buffer_handler(SDL_Window* window)
{
    static GLint program = 0;
    playerent* local_player = reinterpret_cast<playerent*>(*player1_field);
    sqr* world = reinterpret_cast<sqr*>(*world_field);

    if (!user_context)
    {
        FT_Library ft;
        FT_Face face;
        assert(glewInit() == GLEW_OK);
        sdl_get_window_size(window, &width, &height);
        original_context = sdl_get_current(window);
        user_context = sdl_create_contex(window);
        sdl_make_current(window, user_context);

        screen_center_x = width / 2;
        screen_center_y = height / 2;

        program = load_program_shader(vshader, fshader);
        assert(program != -1);

        assert(FT_Init_FreeType(&ft) == 0);

        std::filesystem::path path_font = std::filesystem::current_path();
        path_font += "/esp.ttf";
        std::string tmp_str(path_font);
        assert(FT_New_Face(ft, tmp_str.c_str(), 0, &face) == 0);

        FT_Set_Pixel_Sizes(face, 0, 48);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        assert(load_font_ascii(face, characters_cache) == 0);

        glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glEnable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glBindTexture(GL_TEXTURE_2D, 0);
        FT_Done_Face(face);
        FT_Done_FreeType(ft);

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

    }
    else
    {
        sdl_make_current(window, user_context);
    }

    for (int i = 0; i < ents->length(); i++)
    {
        entity* ent = ents->getbuf();

        const char* name_ent;
        const vec* color_ent;
        vec result (0, 0, 0);

        uchar type = ent[i].type;
        if (type == I_HEALTH && esp_config.health)
        {
            color_ent = &health_color;
            name_ent = health_str;
        }
        else if (type == I_AMMO && esp_config.ammo)
        {
            color_ent = &ammo_color;
            name_ent = ammo_str;
        }
        else if (type == I_AKIMBO && esp_config.akimbo)
        {
            color_ent = &akimbo_color;
            name_ent = akimbo_str;
        }
        else if (type == I_HELMET && esp_config.helmet)
        {
            color_ent = &helmet_color;
            name_ent = helmet_str;
        }
        else if (type == I_ARMOUR && esp_config.armour)
        {
            color_ent = &armour_color;
            name_ent = armour_str;
        }
        else if (type == I_GRENADE && esp_config.granade)
        {
            color_ent = &grenade_color;
            name_ent = grenade_str;
        } else if (type == CTF_FLAG && esp_config.flag)
        {
            color_ent = &flag_color;
            name_ent = flag_str;
        } else if (type == I_CLIPS && esp_config.clips)
        {
            color_ent = &clips_color;
            name_ent = clips_str;
        }
        else
        {
            continue;
        }

        if (!(ent[i].spawned))
        {
            continue;
        }

        float x = ent[i].x;
        float y = ent[i].y;
        float z = (float)(1 + sin(100.0f + x + y) / 20);
        vec pos (x, y, z + SWS(world, (int) x, (int) y,  *sfactor)->floor);

        if (!world_to_screen(pos, result, modelviewproj->v, width, height))
            continue;

        float distance = distance_vec(pos, local_player->o);
        float scale = (float) fast_divide((double) 10.0f, (double) distance);

        if (scale < 0.20f)
            scale = 0.20f;

        std::string tmp_name_ent(name_ent);
        render_text(program, tmp_name_ent, result.x, result.y, scale, color_ent);
    }

    for (int i = 0; i < players->length(); i++)
    {
        float lowestHypot = MAXFLOAT;
        playerent** player_array = players->buf;
        playerent* player = player_array[i];

        if (!esp_config.player)
            break;

        if (player == nullptr)
            continue;
        else if (player->state != CS_ALIVE)
            continue;

        vec pos (player->o.x, player->o.y, player->o.z);
        vec result;

        if (!world_to_screen(pos, result, modelviewproj->v, width, height))
            continue;

        if (aimbot_config.shoot && player->team != local_player->team)
        {
            float multiplier = screen_center_x / int(*fov);
            float size_aim_fov = (aimbot_config.aimbot_fov * multiplier);
            int diff_x = screen_center_x - result.x;
            int diff_y = screen_center_y - result.y;

            float hypotenuse = sqrt((diff_x * diff_x) + (diff_y * diff_y));
            if (hypotenuse < size_aim_fov && hypotenuse < lowestHypot) {
                lowestHypot = hypotenuse;

                local_player->attacking = true;
                sendsht(local_player, player->o);
                local_player->attacking = false;
            }
        }

        float distance = distance_vec(player->o, local_player->o);
        float scale = (float) fast_divide((double) 10.0f, (double) distance);

        if (scale < 0.20f)
            scale = 0.20f;

        const vec* render_color;
        if (local_player->team == player->team)
            render_color = &frend_color;
        else
            render_color = &enemy_color;

        std::string tmp_name(player->name);
        tmp_name += " - " + std::to_string(player->health);
        render_text(program, tmp_name, result.x, result.y, scale, render_color);
    }
    aimbot_config.shoot = false;

    sdl_make_current(window, original_context);
    sdl_swap_buffer_original(window);
}

bool world_to_screen(vec pos, vec &screen, float matrix[16], int windowWidth, int windowHeight)
{
    vec4 clipCoords;
    clipCoords.x = pos.x * matrix[0] + pos.y * matrix[4] + pos.z * matrix[8] + matrix[12];
    clipCoords.y = pos.x * matrix[1] + pos.y * matrix[5] + pos.z * matrix[9] + matrix[13];
    clipCoords.z = pos.x * matrix[2] + pos.y * matrix[6] + pos.z * matrix[10] + matrix[14];
    clipCoords.w = pos.x * matrix[3] + pos.y * matrix[7] + pos.z * matrix[11] + matrix[15];

    if (clipCoords.w < 0.1f)
        return false;

    vec NDC;
    NDC.x = clipCoords.x / clipCoords.w;
    NDC.y = clipCoords.y / clipCoords.w;
    NDC.z = clipCoords.z / clipCoords.w;

    screen.x = (windowWidth / 2 * NDC.x) + (NDC.x + windowWidth / 2);
    screen.y = (windowHeight / 2 * NDC.y) + (NDC.y + windowHeight / 2);
    return true;
}

GLint load_program_shader(std::string& vertex_shader_code, std::string& fragment_shader_code) {
    int success_vertx = 0;
    int success_fragm = 0;
    int success_link = 0;
    if (vertex_shader_code.empty() || fragment_shader_code.empty())
        return -1;

    int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

    const char *c_str = vertex_shader_code.c_str();
    glShaderSource(vertex_shader, 1, &c_str, NULL);
    glCompileShader(vertex_shader);

    c_str = fragment_shader_code.c_str();
    glShaderSource(fragment_shader, 1, &c_str, NULL);
    glCompileShader(fragment_shader);

    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success_vertx);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success_fragm);

    if (!success_vertx || !success_fragm)
        return -1;

    int shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glGetProgramiv(shader_program, GL_LINK_STATUS, &success_link);
    if (!shader_program)
        return -1;

    return shader_program;
}

int load_font_ascii(FT_Face& face, std::map<GLchar, grafic_character>& cache)
{
    for (unsigned char c = 0; c < 128; c++)
    {
        if(FT_Load_Char(face, c, FT_LOAD_RENDER) != 0)
            return -1;

        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_RED,
                    face->glyph->bitmap.width,
                    face->glyph->bitmap.rows,
                    0,
                    GL_RED,
                    GL_UNSIGNED_BYTE,
                    face->glyph->bitmap.buffer
                    );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        grafic_character grafic_char = {
                    texture,
                    glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
                    glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
                    static_cast<unsigned int>(face->glyph->advance.x)
        };
        cache.insert(std::pair<char, grafic_character>(c, grafic_char));
    }
    return 0;
}

void render_text(GLint program, std::string& text, float x, float y, float scale, const vec* color)
{
    std::vector<grafic_character> grafic_text(text.size());
    glUseProgram(program);
    glUniform3f(glGetUniformLocation(program, "textColor"), color->x, color->y, color->z);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(VAO);

    int i = 0;
    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++, i++)
    {
        grafic_text[i] = characters_cache[*c];
        x -= ((characters_cache[*c].advance >> 6) * scale) / 2;
    }

    for (int i = 0; i < grafic_text.size(); i++)
    {
        grafic_character ch = grafic_text[i];

        float xpos = x + ch.bearing.x * scale;
        float ypos = y - (ch.size.y - ch.bearing.y) * scale;

        float w = ch.size.x * scale;
        float h = ch.size.y * scale;

        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };

        glBindTexture(GL_TEXTURE_2D, ch.texture_id);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (ch.advance >> 6) * scale;
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

char* get_path_exutable()
{
    static char path[4096];
    if (strlen(path) > 0)
        return path;
    if (readlink ("/proc/self/exe", path, sizeof(path)) != -1)
        return path;
    return NULL;
}

struct mmaped_elf_node* search_map_file(const char* const path, struct mmaped_elf_node* begin)
{
    struct mmaped_elf_node* itr = begin;

    while (itr != NULL) {
        if (!strcmp(path, begin->path))
            return reinterpret_cast<mmaped_elf_node*> (itr->address);
        itr = itr->next;
    }

    return NULL;
}

int add_file_to_map_list(const char* const path, struct mmaped_elf_node** begin, void** elf_map)
{
    int elf_file_fd = 0;
    int ret_err_val = 0;
    void* file_map_address = NULL;
    struct mmaped_elf_node* end;
    struct mmaped_elf_node* new_node;
    struct stat elf_file_stat;

    if (begin == NULL || path == NULL)
        return -1;

    if ((elf_file_fd = open(path, O_RDONLY)) < 0)
        return -2;

    if (fstat(elf_file_fd, &elf_file_stat) > 0)
        return -3;

    new_node = (struct mmaped_elf_node*) malloc(sizeof(struct mmaped_elf_node));
    if (new_node == NULL)
    {
        close(elf_file_fd);
        return -4;
    }

    file_map_address = mmap(0, elf_file_stat.st_size, PROT_READ, MAP_PRIVATE, elf_file_fd, 0);
    if (file_map_address == NULL)
    {
        free(new_node);
        close(elf_file_fd);
        return -5;
    }

    new_node->address = file_map_address;
    snprintf(new_node->path, 4096, "%s", path);
    *elf_map = file_map_address;

    if (*begin == NULL) {
        *begin = new_node;
        return 0;
    }

    end = *begin;
    while (end->next) end = end->next;
    end->next = new_node;
    return 0;
}

int resolve_symtab_symbol(struct dl_phdr_info *info, size_t size, void *data)
{
    void* maped_elf_file = NULL;
    const char* path_exutable = NULL;
    char* sh_strtab = NULL;
    char* strtab = NULL;
    Elf64_Ehdr* ehdr = NULL;
    Elf64_Shdr* shdr = NULL;
    Elf64_Shdr* shdr_symtab = NULL;
    Elf64_Shdr* shdr_strtab = NULL;
    Elf64_Shdr* shdr_shstrtab = NULL;
    Elf64_Sym* symtab = NULL;
    struct resolve_sym_info* sym_info = ( struct resolve_sym_info*) data;

    if (*info->dlpi_name == 0)
        path_exutable = get_path_exutable();
    else
        path_exutable = info->dlpi_name;

    if (strcmp(basename(path_exutable), sym_info->name_shared_object))
        return 0;

    maped_elf_file = search_map_file(path_exutable, map_list_shared);
    if (maped_elf_file == NULL)
        add_file_to_map_list(path_exutable, &map_list_shared,  &maped_elf_file);

    assert(maped_elf_file != NULL);

    ehdr = reinterpret_cast<Elf64_Ehdr*>(maped_elf_file);
    shdr = reinterpret_cast<Elf64_Shdr*>(ehdr->e_shoff + reinterpret_cast<long>(maped_elf_file));
    shdr_shstrtab = ehdr->e_shstrndx + shdr;
    sh_strtab = reinterpret_cast<char*>(shdr_shstrtab->sh_offset + reinterpret_cast<long>(maped_elf_file));

    shdr_symtab = shdr;
    while(shdr + ehdr->e_shnum > shdr_symtab && strcmp(sh_strtab + shdr_symtab->sh_name, ".symtab"))
        shdr_symtab++;

    shdr_strtab = shdr;
    while(shdr + ehdr->e_shnum > shdr_strtab && strcmp(sh_strtab + shdr_strtab->sh_name, ".strtab"))
        shdr_strtab++;

    assert(!strcmp(sh_strtab + shdr_symtab->sh_name, ".symtab"));
    assert(!strcmp(sh_strtab + shdr_strtab->sh_name, ".strtab"));

    symtab = reinterpret_cast<Elf64_Sym*>(shdr_symtab->sh_offset + reinterpret_cast<long>(maped_elf_file));
    strtab = reinterpret_cast<char*>(shdr_strtab->sh_offset + reinterpret_cast<long>(maped_elf_file));
    for (int i = 0; i < shdr_symtab->sh_size / shdr_symtab->sh_entsize; i++)
    {
        if (!strcmp(strtab + symtab[i].st_name, sym_info->symbol)) {
            sym_info->address_symbol = reinterpret_cast<void*>(info->dlpi_addr + symtab[i].st_value);
            break;
        }
    }

    return 0;
}

int hook_global_offset_table(struct dl_phdr_info *info, size_t size, void *data)
{
    const Elf64_Phdr* begin_phdr = NULL;
    const Elf64_Phdr* dynamic_phdr = NULL;
    Elf64_Dyn* dynamic = NULL;
    Elf64_Sym* symtab = NULL;
    Elf64_Rel* rel_plt = NULL;
    Elf64_Rela* rel_addend_plt = NULL;
    Elf64_Addr rel_plt_ptr = 0;
    int plt_rel_size = 0;
    int reloc_struct_type = 0;
    int reloc_strcut_size = 0;
    const char* path_exutable = NULL;
    char* strtab = NULL;
    struct hook_relocation_info* hook_info = (struct hook_relocation_info*) data;

    if (*info->dlpi_name == 0)
        path_exutable = get_path_exutable();
    else
        path_exutable = info->dlpi_name;

    if (strcmp(basename(path_exutable), hook_info->name_shared_object))
        return 0;

    begin_phdr = info->dlpi_phdr;
    dynamic_phdr = begin_phdr;
    while (begin_phdr + info->dlpi_phnum > dynamic_phdr && dynamic_phdr->p_type != PT_DYNAMIC)
        dynamic_phdr++;

    assert(dynamic_phdr->p_type == PT_DYNAMIC);

    dynamic = (Elf64_Dyn*) (dynamic_phdr->p_vaddr + info->dlpi_addr);
    while (dynamic->d_tag != DT_NULL)
    {
        if (dynamic->d_tag == DT_JMPREL)
        rel_plt_ptr = dynamic->d_un.d_ptr;
        else if (dynamic->d_tag == DT_PLTREL)
            reloc_struct_type = dynamic->d_un.d_val;
        else if (dynamic->d_tag == DT_SYMTAB)
            symtab = (Elf64_Sym*) dynamic->d_un.d_ptr;
        else if (dynamic->d_tag == DT_STRTAB)
            strtab = (char*) dynamic->d_un.d_ptr;
        else if (dynamic->d_tag == DT_PLTRELSZ)
            plt_rel_size = dynamic->d_un.d_val;
        else if (dynamic->d_tag == DT_RELENT)
            reloc_strcut_size = dynamic->d_un.d_val;
        else if (dynamic->d_tag == DT_RELAENT)
            reloc_strcut_size = dynamic->d_un.d_val;

        dynamic++;
    }

    assert(plt_rel_size != 0);
    assert(reloc_struct_type != 0);
    assert(rel_plt_ptr != 0);
    assert(reloc_strcut_size != 0);
    assert(strtab != NULL);
    assert(symtab != NULL);

    if (reloc_struct_type == DT_RELA)
        rel_addend_plt = (Elf64_Rela*) rel_plt_ptr;
    else
        rel_plt = (Elf64_Rel*) rel_plt_ptr;

    for (int i = 0; i <  plt_rel_size / reloc_strcut_size; i++)
    {
        int index_sym = 0;
        int64_t* got_entry = NULL;

        if (reloc_struct_type == DT_RELA)
        {
            index_sym = ELF64_R_SYM(rel_addend_plt[i].r_info);
            got_entry = reinterpret_cast<int64_t*>(rel_addend_plt[i].r_offset + info->dlpi_addr);
        }
        else
        {
            index_sym = ELF64_R_SYM(rel_plt[i].r_info);
            got_entry = reinterpret_cast<int64_t*>(rel_addend_plt[i].r_offset + info->dlpi_addr);
        }

        Elf64_Sym symbol = symtab[index_sym];
        if (!strcmp(strtab + symbol.st_name, hook_info->symbol_name)) {
            hook_info->prev_address = (void*) *got_entry;
            *got_entry = (int64_t) hook_info->new_address;
        }
    }
    return 0;
}

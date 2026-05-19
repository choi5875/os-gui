#ifndef APP_H
#define APP_H

#include <string>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#define WIDTH 900
#define HEIGHT 650
#define REFRESH_MS 500

enum SortMode {
    SORT_CPU,
    SORT_MEMORY,
    SORT_PID
};

typedef struct ProcessEntry {
    int pid;
    std::string name;
    double cpu_percent;
    double memory_mb;
    std::uint64_t total_jiffies;
} ProcessEntry;

typedef struct GProcess {
    SDL_Texture *pid_text;
    SDL_Texture *name_text;
    SDL_Texture *cpu_text;
    SDL_Texture *mem_text;
    SDL_Rect pid_pos;
    SDL_Rect name_pos;
    SDL_Rect cpu_pos;
    SDL_Rect mem_pos;
    SDL_Rect row_pos;
} GProcess;

typedef struct SortButton {
    SDL_Rect rect;
    std::string label;
    SortMode mode;
} SortButton;

typedef struct AppData {
    std::filesystem::path current_directory;
    TTF_Font *font;
    TTF_Font *small_font;
    SDL_Texture *logo;

    std::vector<ProcessEntry *> process_entries;
    std::vector<GProcess *> graphic_entries;

    std::vector<std::pair<int, std::uint64_t>> previous_process_jiffies;
    std::uint64_t previous_total_jiffies;
    std::uint32_t last_refresh_tick;

    std::vector<SortButton> sort_buttons;
    SortMode sort_mode;
    int selected_pid;
    int scroll_offset;
} AppData;

void initialize(SDL_Renderer *renderer, AppData *data_ptr);
void handleEvent(SDL_Event *event, SDL_Renderer *renderer, AppData *data_ptr);
void render(SDL_Renderer *renderer, AppData *data_ptr);
void populateProcesses(SDL_Renderer *renderer, AppData *data_ptr);
void clearGProcesses(std::vector<GProcess *> &graphic_entries);
void clearProcessEntries(std::vector<ProcessEntry *> &entries);
bool compareProcessEntries(const ProcessEntry *a, const ProcessEntry *b, SortMode mode);
bool pointInRect(int x, int y, SDL_Rect &rect);
void quit(AppData *data_ptr);

#endif

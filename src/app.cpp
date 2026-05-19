#include "app.h"

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>

#include <SDL2/SDL_image.h>

static const char *chooseFontPath()
{
    const std::vector<const char *> candidates = {
        "resrc/fonts/OpenSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    };

    for (const char *path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return NULL;
}

static bool readTotalCpuJiffies(std::uint64_t &total)
{
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::getline(file, line);

    std::istringstream iss(line);
    std::string label;
    iss >> label;
    if (label != "cpu") {
        return false;
    }

    total = 0;
    std::uint64_t value = 0;
    while (iss >> value) {
        total += value;
    }

    return true;
}

static bool readProcessStat(int pid, ProcessEntry *entry)
{
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::getline(file, line);

    std::size_t open = line.find('(');
    std::size_t close = line.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return false;
    }

    entry->pid = pid;
    entry->name = line.substr(open + 1, close - open - 1);

    std::string after = line.substr(close + 2);
    std::istringstream iss(after);
    std::vector<std::string> fields;
    std::string token;
    while (iss >> token) {
        fields.push_back(token);
    }

    if (fields.size() < 15) {
        return false;
    }

    entry->total_jiffies = std::stoull(fields[11]) + std::stoull(fields[12]);
    return true;
}

static bool readMemoryMB(int pid, double &memory_mb)
{
    std::ifstream file("/proc/" + std::to_string(pid) + "/status");
    if (!file.is_open()) {
        return false;
    }

    std::string key;
    while (file >> key) {
        if (key == "VmRSS:") {
            long kb = 0;
            file >> kb;
            memory_mb = static_cast<double>(kb) / 1024.0;
            return true;
        }
        std::string skip;
        std::getline(file, skip);
    }

    memory_mb = 0.0;
    return true;
}

static std::uint64_t getPrevProcJiffies(AppData *data_ptr, int pid)
{
    for (int i = 0; i < static_cast<int>(data_ptr->previous_process_jiffies.size()); i++) {
        if (data_ptr->previous_process_jiffies[i].first == pid) {
            return data_ptr->previous_process_jiffies[i].second;
        }
    }
    return 0;
}

static std::string formatDouble(double value, int decimals)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimals) << value;
    return oss.str();
}

static SDL_Texture *createTextTexture(SDL_Renderer *renderer, TTF_Font *font, const std::string &text, SDL_Color color)
{
    if (!renderer || !font) {
        return NULL;
    }

    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) {
        return NULL;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

static SDL_Texture *createFallbackLogo(SDL_Renderer *renderer)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 48, 48, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        return NULL;
    }

    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 55, 92, 142, 255));
    SDL_Rect inner = {8, 8, 32, 32};
    SDL_FillRect(surf, &inner, SDL_MapRGBA(surf->format, 97, 165, 194, 255));

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

void initialize(SDL_Renderer *renderer, AppData *data_ptr)
{
    const char *font_path = chooseFontPath();
    data_ptr->font = NULL;
    data_ptr->small_font = NULL;
    if (font_path) {
        data_ptr->font = TTF_OpenFont(font_path, 19);
        data_ptr->small_font = TTF_OpenFont(font_path, 15);
    }

    if (!data_ptr->font || !data_ptr->small_font) {
        std::cerr << "Warning: font load failed." << std::endl;
    }

    SDL_Surface *logo_surf = IMG_Load("resrc/images/linux-penguin.png");
    if (logo_surf) {
        data_ptr->logo = SDL_CreateTextureFromSurface(renderer, logo_surf);
        SDL_FreeSurface(logo_surf);
    } else {
        data_ptr->logo = createFallbackLogo(renderer);
    }

    data_ptr->sort_buttons.push_back({{20, 80, 130, 32}, "Sort CPU", SORT_CPU});
    data_ptr->sort_buttons.push_back({{160, 80, 130, 32}, "Sort Mem", SORT_MEMORY});
    data_ptr->sort_buttons.push_back({{300, 80, 130, 32}, "Sort PID", SORT_PID});

    data_ptr->sort_mode = SORT_CPU;
    data_ptr->selected_pid = -1;
    data_ptr->scroll_offset = 0;
    data_ptr->last_refresh_tick = 0;
    data_ptr->previous_total_jiffies = 0;

    readTotalCpuJiffies(data_ptr->previous_total_jiffies);
    populateProcesses(renderer, data_ptr);
}

void handleEvent(SDL_Event *event, SDL_Renderer *renderer, AppData *data_ptr)
{
    (void)renderer;

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x;
        int y = event->button.y;

        for (int i = 0; i < static_cast<int>(data_ptr->sort_buttons.size()); i++) {
            if (pointInRect(x, y, data_ptr->sort_buttons[i].rect)) {
                data_ptr->sort_mode = data_ptr->sort_buttons[i].mode;
                clearGProcesses(data_ptr->graphic_entries);
                clearProcessEntries(data_ptr->process_entries);
                populateProcesses(renderer, data_ptr);
                return;
            }
        }

        for (int i = 0; i < static_cast<int>(data_ptr->graphic_entries.size()); i++) {
            if (pointInRect(x, y, data_ptr->graphic_entries[i]->row_pos)) {
                data_ptr->selected_pid = data_ptr->process_entries[i]->pid;
                break;
            }
        }
    } else if (event->type == SDL_MOUSEWHEEL) {
        data_ptr->scroll_offset -= 2 * event->wheel.y;
        if (data_ptr->scroll_offset < 0) {
            data_ptr->scroll_offset = 0;
        }

        int max_offset = std::max(0, static_cast<int>(data_ptr->graphic_entries.size()) - 14);
        if (data_ptr->scroll_offset > max_offset) {
            data_ptr->scroll_offset = max_offset;
        }
    }
}

void render(SDL_Renderer *renderer, AppData *data_ptr)
{
    SDL_Color text_color = {240, 240, 240, 255};
    SDL_Color muted_color = {190, 198, 210, 255};

    SDL_SetRenderDrawColor(renderer, 18, 30, 48, 255);
    SDL_RenderClear(renderer);

    SDL_Rect top = {0, 0, WIDTH, 60};
    SDL_SetRenderDrawColor(renderer, 26, 49, 78, 255);
    SDL_RenderFillRect(renderer, &top);

    if (data_ptr->logo) {
        SDL_Rect logo_pos = {10, 8, 44, 44};
        SDL_RenderCopy(renderer, data_ptr->logo, NULL, &logo_pos);
    }

    SDL_Texture *title_tex = createTextTexture(renderer, data_ptr->font, "OS GUI - task manager", text_color);
    SDL_Texture *hint_tex = createTextTexture(renderer, data_ptr->small_font, "Click to view details | Use mouse scroll to scroll", muted_color);

    SDL_Rect rect = {65, 18, 0, 0};
    if (title_tex) {
        SDL_QueryTexture(title_tex, NULL, NULL, &rect.w, &rect.h);
        SDL_RenderCopy(renderer, title_tex, NULL, &rect);
        SDL_DestroyTexture(title_tex);
    }

    rect.x = 20;
    rect.y = 120;
    if (hint_tex) {
        SDL_QueryTexture(hint_tex, NULL, NULL, &rect.w, &rect.h);
        SDL_RenderCopy(renderer, hint_tex, NULL, &rect);
        SDL_DestroyTexture(hint_tex);
    }

    for (int i = 0; i < static_cast<int>(data_ptr->sort_buttons.size()); i++) {
        bool active = data_ptr->sort_buttons[i].mode == data_ptr->sort_mode;
        SDL_SetRenderDrawColor(renderer, active ? 71 : 45, active ? 124 : 82, active ? 170 : 118, 255);
        SDL_RenderFillRect(renderer, &data_ptr->sort_buttons[i].rect);
        SDL_SetRenderDrawColor(renderer, 210, 220, 230, 255);
        SDL_RenderDrawRect(renderer, &data_ptr->sort_buttons[i].rect);

        SDL_Texture *label_tex = createTextTexture(renderer, data_ptr->small_font, data_ptr->sort_buttons[i].label, text_color);
        if (label_tex) {
            SDL_Rect label_pos = {data_ptr->sort_buttons[i].rect.x + 18, data_ptr->sort_buttons[i].rect.y + 8, 0, 0};
            SDL_QueryTexture(label_tex, NULL, NULL, &label_pos.w, &label_pos.h);
            SDL_RenderCopy(renderer, label_tex, NULL, &label_pos);
            SDL_DestroyTexture(label_tex);
        }
    }

    SDL_Rect table = {20, 145, 620, 485};
    SDL_Rect details = {655, 145, 225, 485};
    SDL_SetRenderDrawColor(renderer, 29, 42, 60, 255);
    SDL_RenderFillRect(renderer, &table);
    SDL_RenderFillRect(renderer, &details);
    SDL_SetRenderDrawColor(renderer, 90, 113, 138, 255);
    SDL_RenderDrawRect(renderer, &table);
    SDL_RenderDrawRect(renderer, &details);

    SDL_Texture *hdr_pid = createTextTexture(renderer, data_ptr->small_font, "PID", muted_color);
    SDL_Texture *hdr_name = createTextTexture(renderer, data_ptr->small_font, "Name", muted_color);
    SDL_Texture *hdr_cpu = createTextTexture(renderer, data_ptr->small_font, "CPU%", muted_color);
    SDL_Texture *hdr_mem = createTextTexture(renderer, data_ptr->small_font, "Mem MB", muted_color);

    SDL_Rect hpid = {30, 152, 0, 0};
    SDL_Rect hname = {95, 152, 0, 0};
    SDL_Rect hcpu = {350, 152, 0, 0};
    SDL_Rect hmem = {455, 152, 0, 0};

    if (hdr_pid) {
        SDL_QueryTexture(hdr_pid, NULL, NULL, &hpid.w, &hpid.h);
        SDL_RenderCopy(renderer, hdr_pid, NULL, &hpid);
        SDL_DestroyTexture(hdr_pid);
    }
    if (hdr_name) {
        SDL_QueryTexture(hdr_name, NULL, NULL, &hname.w, &hname.h);
        SDL_RenderCopy(renderer, hdr_name, NULL, &hname);
        SDL_DestroyTexture(hdr_name);
    }
    if (hdr_cpu) {
        SDL_QueryTexture(hdr_cpu, NULL, NULL, &hcpu.w, &hcpu.h);
        SDL_RenderCopy(renderer, hdr_cpu, NULL, &hcpu);
        SDL_DestroyTexture(hdr_cpu);
    }
    if (hdr_mem) {
        SDL_QueryTexture(hdr_mem, NULL, NULL, &hmem.w, &hmem.h);
        SDL_RenderCopy(renderer, hdr_mem, NULL, &hmem);
        SDL_DestroyTexture(hdr_mem);
    }

    SDL_RenderDrawLine(renderer, 26, 176, 634, 176);

    int start = data_ptr->scroll_offset;
    int visible = 14;
    int end = std::min(static_cast<int>(data_ptr->graphic_entries.size()), start + visible);

    for (int i = start; i < end; i++) {
        int draw_row = i - start;
        int y_shift = 182 + draw_row * 32;

        data_ptr->graphic_entries[i]->row_pos.y = y_shift;
        data_ptr->graphic_entries[i]->row_pos.h = 30;

        data_ptr->graphic_entries[i]->pid_pos.y = y_shift + 6;
        data_ptr->graphic_entries[i]->name_pos.y = y_shift + 6;
        data_ptr->graphic_entries[i]->cpu_pos.y = y_shift + 6;
        data_ptr->graphic_entries[i]->mem_pos.y = y_shift + 6;

        bool selected = data_ptr->selected_pid == data_ptr->process_entries[i]->pid;
        SDL_SetRenderDrawColor(renderer, selected ? 60 : 34, selected ? 95 : 53, selected ? 130 : 73, 255);
        SDL_RenderFillRect(renderer, &data_ptr->graphic_entries[i]->row_pos);

        if (data_ptr->graphic_entries[i]->pid_text) {
            SDL_RenderCopy(renderer, data_ptr->graphic_entries[i]->pid_text, NULL, &data_ptr->graphic_entries[i]->pid_pos);
        }
        if (data_ptr->graphic_entries[i]->name_text) {
            SDL_RenderCopy(renderer, data_ptr->graphic_entries[i]->name_text, NULL, &data_ptr->graphic_entries[i]->name_pos);
        }
        if (data_ptr->graphic_entries[i]->cpu_text) {
            SDL_RenderCopy(renderer, data_ptr->graphic_entries[i]->cpu_text, NULL, &data_ptr->graphic_entries[i]->cpu_pos);
        }
        if (data_ptr->graphic_entries[i]->mem_text) {
            SDL_RenderCopy(renderer, data_ptr->graphic_entries[i]->mem_text, NULL, &data_ptr->graphic_entries[i]->mem_pos);
        }
    }

    ProcessEntry *selected_entry = NULL;
    for (int i = 0; i < static_cast<int>(data_ptr->process_entries.size()); i++) {
        if (data_ptr->process_entries[i]->pid == data_ptr->selected_pid) {
            selected_entry = data_ptr->process_entries[i];
            break;
        }
    }

    SDL_Texture *detail_title = createTextTexture(renderer, data_ptr->small_font, "Details", text_color);
    if (detail_title) {
        SDL_Rect d = {668, 152, 0, 0};
        SDL_QueryTexture(detail_title, NULL, NULL, &d.w, &d.h);
        SDL_RenderCopy(renderer, detail_title, NULL, &d);
        SDL_DestroyTexture(detail_title);
    }

    if (selected_entry) {
        SDL_Texture *d1 = createTextTexture(renderer, data_ptr->small_font, "PID: " + std::to_string(selected_entry->pid), text_color);
        SDL_Texture *d2 = createTextTexture(renderer, data_ptr->small_font, "Name: " + selected_entry->name, text_color);
        SDL_Texture *d3 = createTextTexture(renderer, data_ptr->small_font, "CPU: " + formatDouble(selected_entry->cpu_percent, 2) + "%", text_color);
        SDL_Texture *d4 = createTextTexture(renderer, data_ptr->small_font, "Mem: " + formatDouble(selected_entry->memory_mb, 2) + " MB", text_color);

        SDL_Rect d = {668, 186, 0, 0};
        if (d1) { SDL_QueryTexture(d1, NULL, NULL, &d.w, &d.h); SDL_RenderCopy(renderer, d1, NULL, &d); SDL_DestroyTexture(d1); }
        d.y += 28;
        if (d2) { SDL_QueryTexture(d2, NULL, NULL, &d.w, &d.h); SDL_RenderCopy(renderer, d2, NULL, &d); SDL_DestroyTexture(d2); }
        d.y += 28;
        if (d3) { SDL_QueryTexture(d3, NULL, NULL, &d.w, &d.h); SDL_RenderCopy(renderer, d3, NULL, &d); SDL_DestroyTexture(d3); }
        d.y += 28;
        if (d4) { SDL_QueryTexture(d4, NULL, NULL, &d.w, &d.h); SDL_RenderCopy(renderer, d4, NULL, &d); SDL_DestroyTexture(d4); }

        SDL_Rect bar_back = {668, 306, 190, 20};
        SDL_Rect bar_fill = {668, 306, static_cast<int>(190.0 * std::min(1.0, selected_entry->memory_mb / 1024.0)), 20};
        SDL_SetRenderDrawColor(renderer, 52, 62, 79, 255);
        SDL_RenderFillRect(renderer, &bar_back);
        SDL_SetRenderDrawColor(renderer, 82, 178, 116, 255);
        SDL_RenderFillRect(renderer, &bar_fill);
        SDL_SetRenderDrawColor(renderer, 178, 192, 205, 255);
        SDL_RenderDrawRect(renderer, &bar_back);
    } else {
        SDL_Texture *empty = createTextTexture(renderer, data_ptr->small_font, "Click a row to inspect", muted_color);
        if (empty) {
            SDL_Rect d = {668, 186, 0, 0};
            SDL_QueryTexture(empty, NULL, NULL, &d.w, &d.h);
            SDL_RenderCopy(renderer, empty, NULL, &d);
            SDL_DestroyTexture(empty);
        }
    }

    SDL_RenderPresent(renderer);
}

void populateProcesses(SDL_Renderer *renderer, AppData *data_ptr)
{
    std::uint64_t current_total = 0;
    if (!readTotalCpuJiffies(current_total)) {
        return;
    }

    std::uint64_t total_delta = 0;
    if (current_total > data_ptr->previous_total_jiffies) {
        total_delta = current_total - data_ptr->previous_total_jiffies;
    }

    SDL_Color color = {240, 240, 240, 255};

    try {
        std::filesystem::directory_iterator it(data_ptr->current_directory, std::filesystem::directory_options::skip_permission_denied);
        for (; it != std::filesystem::end(it); it++) {
            const std::filesystem::directory_entry &entry = *it;
            const std::string entry_name = entry.path().filename().string();
            if (entry_name.empty()) {
                continue;
            }

            bool numeric = true;
            for (int j = 0; j < static_cast<int>(entry_name.size()); j++) {
                if (!std::isdigit(static_cast<unsigned char>(entry_name[j]))) {
                    numeric = false;
                    break;
                }
            }
            if (!numeric) {
                continue;
            }

            int pid = 0;
            try {
                pid = std::stoi(entry_name);
            } catch (...) {
                continue;
            }

            ProcessEntry *proc = new ProcessEntry();
            proc->pid = pid;
            proc->cpu_percent = 0.0;
            proc->memory_mb = 0.0;
            proc->total_jiffies = 0;

            if (!readProcessStat(pid, proc)) {
                delete proc;
                continue;
            }

            if (!readMemoryMB(pid, proc->memory_mb)) {
                delete proc;
                continue;
            }

            std::uint64_t prev_proc = getPrevProcJiffies(data_ptr, pid);
            std::uint64_t proc_delta = 0;
            if (proc->total_jiffies > prev_proc) {
                proc_delta = proc->total_jiffies - prev_proc;
            }

            if (total_delta > 0 && prev_proc > 0) {
                proc->cpu_percent = (100.0 * static_cast<double>(proc_delta)) / static_cast<double>(total_delta);
            }

            data_ptr->process_entries.push_back(proc);
        }
    } catch (const std::filesystem::filesystem_error &) {
        return;
    }

    std::sort(data_ptr->process_entries.begin(), data_ptr->process_entries.end(), [data_ptr](const ProcessEntry *a, const ProcessEntry *b) {
        return compareProcessEntries(a, b, data_ptr->sort_mode);
    });

    data_ptr->previous_process_jiffies.clear();
    for (int i = 0; i < static_cast<int>(data_ptr->process_entries.size()); i++) {
        data_ptr->previous_process_jiffies.push_back({data_ptr->process_entries[i]->pid, data_ptr->process_entries[i]->total_jiffies});

        GProcess *g = new GProcess();

        g->pid_text = createTextTexture(renderer, data_ptr->small_font, std::to_string(data_ptr->process_entries[i]->pid), color);
        g->name_text = createTextTexture(renderer, data_ptr->small_font, data_ptr->process_entries[i]->name.substr(0, 22), color);
        g->cpu_text = createTextTexture(renderer, data_ptr->small_font, formatDouble(data_ptr->process_entries[i]->cpu_percent, 1), color);
        g->mem_text = createTextTexture(renderer, data_ptr->small_font, formatDouble(data_ptr->process_entries[i]->memory_mb, 1), color);

        g->row_pos = {26, 182 + (32 * i), 608, 30};

        g->pid_pos.x = 30;
        g->name_pos.x = 95;
        g->cpu_pos.x = 350;
        g->mem_pos.x = 455;

        if (g->pid_text) {
            SDL_QueryTexture(g->pid_text, NULL, NULL, &g->pid_pos.w, &g->pid_pos.h);
        } else {
            g->pid_pos.w = 0;
            g->pid_pos.h = 0;
        }

        if (g->name_text) {
            SDL_QueryTexture(g->name_text, NULL, NULL, &g->name_pos.w, &g->name_pos.h);
        } else {
            g->name_pos.w = 0;
            g->name_pos.h = 0;
        }

        if (g->cpu_text) {
            SDL_QueryTexture(g->cpu_text, NULL, NULL, &g->cpu_pos.w, &g->cpu_pos.h);
        } else {
            g->cpu_pos.w = 0;
            g->cpu_pos.h = 0;
        }

        if (g->mem_text) {
            SDL_QueryTexture(g->mem_text, NULL, NULL, &g->mem_pos.w, &g->mem_pos.h);
        } else {
            g->mem_pos.w = 0;
            g->mem_pos.h = 0;
        }

        g->pid_pos.y = g->row_pos.y + 6;
        g->name_pos.y = g->row_pos.y + 6;
        g->cpu_pos.y = g->row_pos.y + 6;
        g->mem_pos.y = g->row_pos.y + 6;

        data_ptr->graphic_entries.push_back(g);
    }

    data_ptr->previous_total_jiffies = current_total;

    int max_offset = std::max(0, static_cast<int>(data_ptr->graphic_entries.size()) - 14);
    if (data_ptr->scroll_offset > max_offset) {
        data_ptr->scroll_offset = max_offset;
    }
}

bool compareProcessEntries(const ProcessEntry *a, const ProcessEntry *b, SortMode mode)
{
    if (mode == SORT_CPU) {
        return a->cpu_percent > b->cpu_percent;
    }

    if (mode == SORT_MEMORY) {
        return a->memory_mb > b->memory_mb;
    }

    return a->pid < b->pid;
}

void clearGProcesses(std::vector<GProcess *> &graphic_entries)
{
    for (int i = 0; i < static_cast<int>(graphic_entries.size()); i++) {
        if (graphic_entries[i]->pid_text) {
            SDL_DestroyTexture(graphic_entries[i]->pid_text);
        }
        if (graphic_entries[i]->name_text) {
            SDL_DestroyTexture(graphic_entries[i]->name_text);
        }
        if (graphic_entries[i]->cpu_text) {
            SDL_DestroyTexture(graphic_entries[i]->cpu_text);
        }
        if (graphic_entries[i]->mem_text) {
            SDL_DestroyTexture(graphic_entries[i]->mem_text);
        }
        delete graphic_entries[i];
    }

    graphic_entries.clear();
}

void clearProcessEntries(std::vector<ProcessEntry *> &entries)
{
    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
        delete entries[i];
    }

    entries.clear();
}

bool pointInRect(int x, int y, SDL_Rect &rect)
{
    if (x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h) {
        return true;
    }

    return false;
}

void quit(AppData *data_ptr)
{
    clearGProcesses(data_ptr->graphic_entries);
    clearProcessEntries(data_ptr->process_entries);

    if (data_ptr->logo) {
        SDL_DestroyTexture(data_ptr->logo);
    }

    if (data_ptr->font) {
        TTF_CloseFont(data_ptr->font);
    }

    if (data_ptr->small_font) {
        TTF_CloseFont(data_ptr->small_font);
    }
}

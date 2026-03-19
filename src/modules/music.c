#define _POSIX_C_SOURCE 200809L

/**
 * music.c — 音乐播放器模块（musicPanel 背景 + madplay 控制）
 *
 * 功能点：
 *   - 使用 assets/images/musicPanel.bmp 作为背景
 *   - 点击：上一首 / 暂停继续 / 下一首 / 退出
 *   - 曲库来源：扫描“可执行文件同级目录”的全部 .mp3 文件
 *   - 退出前停止 madplay 子进程
 */

#include "music.h"
#include "lcd.h"
#include "ui.h"
#include "touch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *const MUSIC_PANEL_BMP_CANDIDATES[] = {
    "assets/images/musicPanel.bmp",
    "../assets/images/musicPanel.bmp",
    "musicPanel.bmp",
};

#define MUSIC_DESIGN_W  800
#define MUSIC_DESIGN_H  480

/* 顶部“当前播放”信息区域（按设计稿坐标） */
#define INFO_X   136
#define INFO_Y   152
#define INFO_W   528
#define INFO_H   110

/* 按钮热区（按 musicPanel 图中图标区域量测） */
typedef enum {
    MBTN_PREV = 0,
    MBTN_TOGGLE,
    MBTN_NEXT,
    MBTN_EXIT,
    MBTN_COUNT
} MusicButtonId;

static Button s_buttons[MBTN_COUNT];

static unsigned short *g_music_bg_rgb565 = NULL;
static int g_music_bg_w = 0;
static int g_music_bg_h = 0;

static pid_t g_player_pid = -1;
static int g_paused = 0;
static char g_mp3_found_dir[PATH_MAX] = "";

static int scale_x(int x) { return x * g_lcd_width / MUSIC_DESIGN_W; }
static int scale_y(int y) { return y * g_lcd_height / MUSIC_DESIGN_H; }

typedef struct {
    char **items;
    int count;
    int cap;
} Mp3List;

static int has_mp3_suffix(const char *name)
{
    size_t n = strlen(name);
    return (n >= 4) && (strcasecmp(name + n - 4, ".mp3") == 0);
}

static int cmp_str_ptr(const void *a, const void *b)
{
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

static void mp3_list_free(Mp3List *list)
{
    if (list == NULL) return;
    for (int i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int mp3_list_push(Mp3List *list, const char *fullpath)
{
    if (list->count == list->cap) {
        int next_cap = (list->cap == 0) ? 8 : list->cap * 2;
        char **tmp = (char **)realloc(list->items, (size_t)next_cap * sizeof(char *));
        if (tmp == NULL)
            return -1;
        list->items = tmp;
        list->cap = next_cap;
    }

    list->items[list->count] = strdup(fullpath);
    if (list->items[list->count] == NULL)
        return -1;

    list->count++;
    return 0;
}

static int get_executable_dir(char *out_dir, size_t out_sz)
{
    char exe_path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0 || (size_t)n >= sizeof(exe_path))
        return -1;
    exe_path[n] = '\0';

    char *slash = strrchr(exe_path, '/');
    if (slash == NULL)
        return -1;

    *slash = '\0';
    if (snprintf(out_dir, out_sz, "%s", exe_path) >= (int)out_sz)
        return -1;
    return 0;
}

static int collect_mp3_from_dir(Mp3List *list, const char *dir_path)
{
    if (dir_path == NULL || dir_path[0] == '\0')
        return -1;

    DIR *dir = opendir(dir_path);
    if (dir == NULL)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        if (!has_mp3_suffix(ent->d_name))
            continue;

        char fullpath[PATH_MAX];
        if (snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, ent->d_name) >= (int)sizeof(fullpath))
            continue;

        if (mp3_list_push(list, fullpath) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);

    if (list->count > 1)
        qsort(list->items, (size_t)list->count, sizeof(char *), cmp_str_ptr);

    return (list->count > 0) ? 0 : -1;
}

static int collect_mp3(Mp3List *list)
{
    char exe_dir[PATH_MAX] = "";
    char cwd_dir[PATH_MAX] = "";

    if (get_executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        if (collect_mp3_from_dir(list, exe_dir) == 0) {
            snprintf(g_mp3_found_dir, sizeof(g_mp3_found_dir), "%s", exe_dir);
            return 0;
        }
    }

    if (getcwd(cwd_dir, sizeof(cwd_dir)) != NULL) {
        if (collect_mp3_from_dir(list, cwd_dir) == 0) {
            snprintf(g_mp3_found_dir, sizeof(g_mp3_found_dir), "%s", cwd_dir);
            return 0;
        }
    }

    if (collect_mp3_from_dir(list, "/") == 0) {
        snprintf(g_mp3_found_dir, sizeof(g_mp3_found_dir), "%s", "/");
        return 0;
    }

    g_mp3_found_dir[0] = '\0';
    return -1;
}

static const char *basename_from_path(const char *path)
{
    const char *p = strrchr(path, '/');
    return (p == NULL) ? path : (p + 1);
}

static void player_reap_if_exited(void)
{
    if (g_player_pid <= 0)
        return;

    int status = 0;
    pid_t ret = waitpid(g_player_pid, &status, WNOHANG);
    if (ret == g_player_pid) {
        g_player_pid = -1;
        g_paused = 0;
    }
}

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void player_stop(void)
{
    player_reap_if_exited();
    if (g_player_pid <= 0)
        return;

    /* 若之前被 SIGSTOP 暂停，先 SIGCONT，确保终止信号可立即生效。 */
    if (g_paused)
        kill(g_player_pid, SIGCONT);

    kill(g_player_pid, SIGTERM);

    /* 给 madplay 一点时间优雅退出，超时后强制结束，避免阻塞界面线程。 */
    for (int i = 0; i < 20; i++) {
        pid_t ret = waitpid(g_player_pid, NULL, WNOHANG);
        if (ret == g_player_pid)
            break;
        sleep_ms(10);
    }

    if (waitpid(g_player_pid, NULL, WNOHANG) == 0) {
        kill(g_player_pid, SIGKILL);
        waitpid(g_player_pid, NULL, 0);
    }

    g_player_pid = -1;
    g_paused = 0;
}

static int player_start(const char *mp3_path)
{
    player_stop();

    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        setsid();
        execlp("madplay", "madplay", mp3_path, (char *)NULL);
        _exit(127);
    }

    g_player_pid = pid;
    g_paused = 0;
    return 0;
}

static void player_toggle_pause(void)
{
    player_reap_if_exited();
    if (g_player_pid <= 0)
        return;

    if (!g_paused) {
        kill(g_player_pid, SIGSTOP);
        g_paused = 1;
    } else {
        kill(g_player_pid, SIGCONT);
        g_paused = 0;
    }
}

static int load_bmp_scaled_rgb565(const char *path, unsigned short *dst, int dst_w, int dst_h)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 54) {
        close(fd);
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    const unsigned char *file_map = (const unsigned char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_map == MAP_FAILED)
        return -1;

    unsigned short magic = *(const unsigned short *)(file_map + 0);
    int data_offset = *(const int *)(file_map + 10);
    int bmp_w = *(const int *)(file_map + 18);
    int bmp_h = *(const int *)(file_map + 22);
    short depth = *(const short *)(file_map + 28);

    if (magic != 0x4D42 || (depth != 24 && depth != 32) || bmp_w <= 0 || bmp_h == 0 || data_offset < 54) {
        munmap((void *)file_map, file_size);
        return -1;
    }

    int bpp = depth / 8;
    int abs_h = (bmp_h > 0) ? bmp_h : -bmp_h;
    int bottom_up = (bmp_h > 0);
    int stride = (bmp_w * bpp + 3) & ~3;
    const unsigned char *src = file_map + data_offset;

    for (int y = 0; y < dst_h; y++) {
        int src_y = y * abs_h / dst_h;
        int row = bottom_up ? (abs_h - 1 - src_y) : src_y;
        const unsigned char *row_ptr = src + row * stride;

        for (int x = 0; x < dst_w; x++) {
            int src_x = x * bmp_w / dst_w;
            const unsigned char *p = row_ptr + src_x * bpp;
            int b = p[0];
            int g = p[1];
            int r = p[2];
            dst[y * dst_w + x] = (unsigned short)(((r & 0xF8) << 8) |
                                                  ((g & 0xFC) << 3) |
                                                  (b >> 3));
        }
    }

    munmap((void *)file_map, file_size);
    return 0;
}

static int ensure_music_background_loaded(void)
{
    if (g_music_bg_rgb565 != NULL && g_music_bg_w == g_lcd_width && g_music_bg_h == g_lcd_height)
        return 0;

    free(g_music_bg_rgb565);
    g_music_bg_rgb565 = (unsigned short *)malloc((size_t)g_lcd_width * g_lcd_height * sizeof(unsigned short));
    if (g_music_bg_rgb565 == NULL)
        return -1;

    g_music_bg_w = g_lcd_width;
    g_music_bg_h = g_lcd_height;

    int path_count = (int)(sizeof(MUSIC_PANEL_BMP_CANDIDATES) / sizeof(MUSIC_PANEL_BMP_CANDIDATES[0]));
    for (int i = 0; i < path_count; i++) {
        if (load_bmp_scaled_rgb565(MUSIC_PANEL_BMP_CANDIDATES[i], g_music_bg_rgb565, g_music_bg_w, g_music_bg_h) == 0)
            return 0;
    }

    free(g_music_bg_rgb565);
    g_music_bg_rgb565 = NULL;
    return -1;
}

static void release_music_background(void)
{
    free(g_music_bg_rgb565);
    g_music_bg_rgb565 = NULL;
    g_music_bg_w = 0;
    g_music_bg_h = 0;
}

static void build_hotspots(void)
{
    s_buttons[MBTN_PREV] = (Button){
        scale_x(165), scale_y(274), scale_x(150), scale_y(126),
        "PREV", "", COLOR_MUSIC_MAIN, COLOR_MUSIC_BORDER, NULL
    };
    s_buttons[MBTN_TOGGLE] = (Button){
        scale_x(330), scale_y(250), scale_x(140), scale_y(154),
        "PLAY", "", COLOR_MUSIC_MAIN, COLOR_MUSIC_BORDER, NULL
    };
    s_buttons[MBTN_NEXT] = (Button){
        scale_x(486), scale_y(274), scale_x(150), scale_y(126),
        "NEXT", "", COLOR_MUSIC_MAIN, COLOR_MUSIC_BORDER, NULL
    };
    s_buttons[MBTN_EXIT] = (Button){
        scale_x(599), scale_y(37), scale_x(82), scale_y(54),
        "EXIT", "", COLOR_BTN_EXIT, COLOR_BTN_EXIT_BDR, NULL
    };
}

static void draw_frame(void)
{
    if (ensure_music_background_loaded() == 0) {
        lcd_draw_image(0, 0, g_music_bg_w, g_music_bg_h, g_music_bg_rgb565);
    } else {
        lcd_fill_rect(0, 0, g_lcd_width, g_lcd_height, COLOR_BG_DARK);
        lcd_fill_rect(0, 0, g_lcd_width, 55, COLOR_HEADER_BG);
        lcd_draw_hline(0, 55, g_lcd_width, 3, COLOR_MUSIC_BORDER);
        lcd_draw_string(332, 20, "MUSIC PLAYER", COLOR_WHITE, COLOR_HEADER_BG);
        for (int i = 0; i < MBTN_COUNT; i++)
            ui_draw_button(&s_buttons[i]);
    }
}

static void draw_status(const Mp3List *list, int cur_idx)
{
    int x = scale_x(INFO_X);
    int y = scale_y(INFO_Y);
    int w = scale_x(INFO_W);
    int h = scale_y(INFO_H);
    unsigned short bg = RGB565(245, 245, 245);

    if (w < 160) w = 160;
    if (h < 48) h = 48;

    lcd_fill_rect(x + 2, y + 2, w - 4, h - 4, bg);

    if (list == NULL || list->count <= 0) {
        lcd_draw_string(x + 8, y + 12, "NO MP3 NEXT TO EXECUTABLE", COLOR_BTN_EXIT, bg);
        lcd_draw_string(x + 8, y + 34, "SCAN: EXE DIR -> CWD -> /", COLOR_BLACK, bg);
        return;
    }

    char line1[96];
    char line2[96];
    const char *name = basename_from_path(list->items[cur_idx]);

    snprintf(line1, sizeof(line1), "%d/%d %s", cur_idx + 1, list->count, name);
    if (g_player_pid <= 0)
        snprintf(line2, sizeof(line2), "STATE: STOPPED");
    else
        snprintf(line2, sizeof(line2), "STATE: %s", g_paused ? "PAUSED" : "PLAYING");

    lcd_draw_string(x + 8, y + 10, line1, COLOR_BLACK, bg);
    lcd_draw_string(x + 8, y + 34, line2, COLOR_MUSIC_MAIN, bg);
}

void module_music(void)
{
    Mp3List list = {0};
    int cur = 0;

    build_hotspots();
    draw_frame();

    if (collect_mp3(&list) == 0)
        player_start(list.items[cur]);

    if (list.count > 0)
        printf("[music] mp3 found in: %s (count=%d)\n", g_mp3_found_dir, list.count);
    else
        printf("[music] no mp3 found (tried exe dir, cwd, /)\n");

    draw_status(&list, cur);

    while (1) {
        int x, y;
        if (touch_get_tap(&x, &y) != 0)
            continue;

        player_reap_if_exited();

        const Button *hit = ui_hit_test(s_buttons, MBTN_COUNT, x, y);
        if (hit == NULL)
            continue;

        switch ((MusicButtonId)(hit - s_buttons)) {
            case MBTN_PREV:
                if (list.count > 0) {
                    cur = (cur - 1 + list.count) % list.count;
                    player_start(list.items[cur]);
                }
                break;
            case MBTN_TOGGLE:
                if (list.count > 0) {
                    if (g_player_pid <= 0)
                        player_start(list.items[cur]);
                    else
                        player_toggle_pause();
                }
                break;
            case MBTN_NEXT:
                if (list.count > 0) {
                    cur = (cur + 1) % list.count;
                    player_start(list.items[cur]);
                }
                break;
            case MBTN_EXIT:
                player_stop();
                mp3_list_free(&list);
                release_music_background();
                return;
            default:
                break;
        }

        draw_status(&list, cur);
    }
}

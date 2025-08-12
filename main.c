#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <ncurses.h>
#include <threads.h>

#define NS 1000000000 // Количество наносекунд в секунде
#define DEFAULT_TARGET_TPS 30
#define CURSOR_ID 10 // Номер цветовой пары для курсора
#define CURSOR_SPRITE '*' // Символ курсора, если будет пробел на карте на месте курсора

#define MAPW (COLS-2)
#define MAPH (LINES-2)
#define MAPSIZE (MAPW * MAPH)

#define random() (rand() % 100 / 100.0f)
#define sign(x) (x < 0 ? -1 : 1)
#define canmove(cell) (cell.type == EMPTY || cell.type == WATER) // Проверка клетки на то, что она нетвёрдая
#define canmover(cell, x) (canmove(cell) && (x < MAPW-1)) // Проверка клетки на то, что она нетвёрдая, и проверка на границы справа
#define canmovel(cell, x) (canmove(cell) && (x > 0)) // Проверка клетки на то, что она нетвёрдая, и проверка на границы слева
#define watercanmove(cell) (cell.type == EMPTY) // Проверка на то, может ли в клетку перейти вода
#define watercanmover(cell, x) (watercanmove(cell) && (x < MAPW-1)) // Проверка на то, может ли в клетку перейти вода, границы справа
#define watercanmovel(cell, x) (watercanmove(cell) && (x > 0)) // Проверка на то, может ли в клетку перейти вода, границы слева

// Меняет местами ячейки A и B в таблицах map и nmap
#define cellswap(a, b)                  \
        do {                            \
            nmap[a] = map[b];           \
            nmap[b] = map[a];           \
            map[b] = EMPTY;             \
        } while (0)

#define swap(a, b, t)                   \
        do {                            \
            t = a;                      \
            a = b;                      \
            b = t;                      \
        } while (0) 

enum newcolors {
    COLOR_GRAY = 100,
    COLOR_DARKGRAY,
    COLOR_BROWN,
    COLOR_ORANGE,
    COLOR_BLACK_2,
};

typedef enum {
    EMPTY,
    SAND,
    WATER,
    STONE,
    WOOD,
    ASH,
    FIRE,
    BOMB,
} CellType; // Тип ячейки

typedef struct {
    const char *sprites; // Символы, которые соответствуют клтке
    const char *name; // Название клетки
    short *colors; // Цвета символов
} CellInfo;

typedef struct {
    CellType type;
    bool skip_update; // Надо ли пропустить ячейку, когда её надо будет обновить
    bool skip_render; // Балы ячейка уже выведена в терминал для пропуска во время отрисовки уже напечатаных ячеек
    short timer; // Тайиер, нужен для задержки
} Cell;

typedef struct {
    int x, y;
    CellType brush;
    unsigned short brush_size;
} Cursor;

// Перетасовывает массив чисел
void shuffle(int array[], size_t len) {
    int t;
    for (size_t i = len-1; i > 0; i--) {
        size_t j = rand() % (i+1);
        //size_t j = random() * (i+1);
        swap(array[i], array[j], t);
    }
}

// Поворачивает массив ARRAY с длиной LEN на N позиций вправо
void rotate(int array[], size_t len, int n) {
    if (n == 0) return;
    n = n % len;

    int t[n];
    for (int i = 0; i < n; i++) {
        t[i] = array[len-n + i];
    }
    for (int i = len-1; i >= n; i--) {
        array[i] = array[i-n];
    }
    memcpy(array, t, n*sizeof(int));
}

mtx_t map_mtx;
mtx_t curs_mtx;

void render(WINDOW *window, Cell *map, Cursor cursor, CellInfo cells_info[], char *name) {
    if (name != NULL) {
        wmove(window, 0, 1);
        waddstr(window, name);
    }
    
    mtx_lock(&map_mtx);
    mtx_lock(&curs_mtx);
    for (int y = 0; y < MAPH; y++) {
        for (int x = 0; x < MAPW; x++) {
            int current = y*MAPW + x;
            if (map[current].skip_render) {
                map[current].skip_render = false;
                continue;
            }
            wmove(window, y+1, x+1);
            CellInfo current_cell_info = cells_info[map[current].type];
            if (x >= cursor.x-cursor.brush_size+1 && x <= cursor.x+cursor.brush_size-1 && \
                y >= cursor.y-(cursor.brush_size/2) && y <= cursor.y+(cursor.brush_size/2)) {
                if (map[current].type == EMPTY) {
                    waddch(window, CURSOR_SPRITE | COLOR_PAIR(CURSOR_ID));
                } else {
                    waddch(window, current_cell_info.sprites[0] | COLOR_PAIR(CURSOR_ID));
                }
            } else {
                waddch(window, current_cell_info.sprites[0] | A_PROTECT | COLOR_PAIR(current_cell_info.colors[0]));

                if (map[current].type == FIRE) {
                    //int neighbors[8] = {current-MAPW-1, current-MAPW, current-MAPW+1, current-1, current+1, current+MAPW-1, current+MAPW, current+MAPW+1};
                    int neighbors_x[8] = {x-1, x, x+1, x-1, x+1, x-1, x, x+1};
                    int neighbors_y[8] = {y-1, y-1, y-1, y, y, y+1, y+1, y+1};
                    
                    for (int i = 0; i < 8; i++) {
                        if (neighbors_x[i] >= 0 && neighbors_x[i] <= (MAPW-1) && neighbors_y[i] >= 0 && neighbors_y[i] <= (MAPH-1) && (rand() % 10 < 3)) {
                            map[neighbors_y[i]*MAPW + neighbors_x[i]].skip_render = true;
                            wmove(window, neighbors_y[i]+1, neighbors_x[i]+1);
                            waddch(window, cells_info[FIRE].sprites[rand() % 2] | COLOR_PAIR(cells_info[FIRE].colors[rand() % 2]));
                        }
                    }
                }
            }
        }
    }
    mtx_unlock(&map_mtx);

    CellInfo brush_info = cells_info[cursor.brush];

    wmove(window, 1, 1);
    waddch(window, brush_info.sprites[0] | COLOR_PAIR(brush_info.colors[0]));

    wmove(window, 1, 3);
    char brushsize_str[3];
    sprintf(brushsize_str, "%d", (cursor.brush_size <= 99 ? cursor.brush_size : 99));
    mtx_unlock(&curs_mtx);
    waddstr(window, brushsize_str);

    wmove(window, 1, 6);
    waddstr(window, brush_info.name);

    wrefresh(window);
}

void update(Cell map[]) {
    int order[MAPW]; // Массив с порядком обработки ячеек
    for (int i = 0; i < MAPW; i++)
        order[i] = i;
    shuffle(order, MAPW); // Перемешивание массива, чтобы ячейки обрабатывались в случайном порядке
    
    mtx_lock(&map_mtx);

    for (int y = MAPH-1; y >= 0; y--) { // y - координата ячейки по Y
        rotate(order, MAPW, rand() % MAPW);
        for (int i = 0; i < MAPW; i++) {
            int x = order[i]; // Координата ячейки по x
            int current = y*MAPW + x;
            if (map[current].skip_update) {
                map[current].skip_update = false;
                continue;
            }
            int top = (y-1)*MAPW + x;
            int bottom = (y+1)*MAPW + x;
            Cell t;

            int movements[8] = {0}; // Массив индексов клеток, куда можно переместиться
            int j = 0;

            switch (map[current].type) {
            case EMPTY:
            case WOOD:
            case STONE:
                break;
            case ASH:
            case SAND:
                if (y == MAPH-1) {
                    break;
                }
               if (canmove(map[bottom])) {
                    swap(map[current], map[bottom], t);
                } else if (canmovel(map[bottom - 1], x) && canmover(map[bottom + 1], x)) {
                    int idx = bottom + (rand() % 2 ? 1 : -1);
                    swap(map[current], map[idx], t);
                    //swap(map[current], map[bottom + (rand() % 2 ? 1 : -1)], t);
                } else if (canmovel(map[bottom - 1], x) && !canmover(map[bottom + 1], x)) {
                    swap(map[current], map[bottom - 1], t);
                } else if (!canmovel(map[bottom - 1], x) && canmover(map[bottom + 1], x)) {
                    swap(map[current], map[bottom + 1], t);
                }
                break;
            case WATER:
                if (y < MAPH-1 && watercanmove(map[bottom])) {
                    swap(map[current], map[bottom], t);
                } else if (watercanmovel(map[current - 1], x) && watercanmover(map[current + 1], x) && (y == MAPH-1 || (!watercanmovel(map[bottom - 1], x) && !watercanmover(map[bottom + 1], x)))) {
                    int idx = current + (rand() % 2 ? 1 : -1);
                    swap(map[current], map[idx], t);
                    //swap(map[current], map[current + (rand() % 2 ? 1 : -1)], t);
                } else if (watercanmovel(map[current - 1], x) && !watercanmover(map[current + 1], x) && (y == MAPH-1 || (!watercanmovel(map[bottom - 1], x) && !watercanmover(map[bottom + 1], x)))) {
                    swap(map[current], map[current - 1], t);
                } else if (!watercanmovel(map[current - 1], x) && watercanmover(map[current + 1], x) && (y == MAPH-1 || (!watercanmovel(map[bottom - 1], x) && !watercanmover(map[bottom + 1], x)))) {
                    swap(map[current], map[current + 1], t);
                } else if (y < MAPH-1 && watercanmovel(map[bottom - 1], x) && watercanmover(map[bottom + 1], x)) {
                    int idx = bottom + (rand() % 2 ? 1 : -1);
                    swap(map[current], map[idx], t);
                    //swap(map[current], map[bottom + (rand() % 2 ? 1 : -1)], t);
                } else if (y < MAPH-1 && watercanmovel(map[bottom - 1], x) && !watercanmover(map[bottom + 1], x)) {
                    swap(map[current], map[bottom - 1], t);
                } else if (y < MAPH-1 && !watercanmovel(map[bottom - 1], x) && watercanmover(map[bottom + 1], x)) {
                    swap(map[current], map[bottom + 1], t);
                }
                break;
            case FIRE:
                if (y > 0) {
                    if (map[top].type == WATER) goto fireclear;
                    else if (map[top].type == WOOD) movements[j++] = top;
                    if (x > 0 && map[top - 1].type == WATER) goto fireclear;
                    else if (x > 0 && map[top - 1].type == WOOD) movements[j++] = top-1;
                    if (x < MAPW-1 && map[top + 1].type == WATER) goto fireclear;
                    else if (x < MAPW-1 && map[top + 1].type == WOOD) movements[j++] = top+1;
                }

                if (x > 0 && map[current - 1].type == WATER) goto fireclear;
                else if (x > 0 && map[current - 1].type == WOOD) movements[j++] = current-1;
                if (x < MAPW-1 && map[current + 1].type == WATER) goto fireclear;
                else if (x < MAPW-1 && map[current + 1].type == WOOD) movements[j++] = current+1;
                
                if (y < MAPH-1) {
                    if (map[bottom].type == WATER) goto fireclear;
                    else if (map[bottom].type == WOOD) movements[j++] = bottom;
                    if (x > 0 && map[bottom - 1].type == WATER) goto fireclear;
                    else if (x > 0 && map[bottom - 1].type == WOOD) movements[j++] = bottom - 1;
                    if (x < MAPW-1 && map[bottom + 1].type == WATER) goto fireclear;
                    else if (x < MAPW-1 && map[bottom + 1].type == WOOD) movements[j++] = bottom + 1;
                }
                if (j > 0) {
                    float r = random();

                    if (r > 0.15f)
                        break;

                    j *= random(); // Случайное сичло в диапазоне 0..j
                    map[movements[j]].type = FIRE;
                    if (movements[j] > bottom-1) // Пропуск обновления новой ячейки огня, если она будет ещё раз обрабатываться в цикле за этот кадр
                        map[movements[j]].skip_update = true;
                    if (r < 0.06f) {
                        map[current].type = (r < 0.04f) ? ASH : FIRE;
                    } else {
                        map[current].type = EMPTY;
                    }
                } else {
                    fireclear:
                    map[current].type = EMPTY;
                }
                break;
            case BOMB:
                if (y > 0) {
                    if (map[top].type == FIRE) goto boom;
                    if (x > 0 && map[top - 1].type == FIRE) goto boom;
                    if (x < MAPW-1 && map[top + 1].type == FIRE) goto boom;
                }
                if (x > 0 && map[current - 1].type == FIRE) goto boom;
                if (x < MAPW-1 && map[current + 1].type == FIRE) goto boom;
                if (y < MAPH-1) {
                    if (map[bottom].type == FIRE) goto boom;
                    if (x > 0 && map[bottom - 1].type == FIRE) goto boom;
                    if (x < MAPW-1 && map[bottom + 1].type == FIRE) goto boom;
                }
                if (map[current].timer >= 50) {
                    boom:
                    for (int cy = y-3; cy <= y+3; cy++) {
                        for (int cx = x-7; cx <= x+7; cx++) {
                            if (cx >= 0 && cx <= (MAPW-1) && cy >= 0 && cy <= (MAPH-1)) {
                                if (rand() % 6 == 0) continue;
                                
                                int ncurrent = cy*MAPW + cx;

                                if (cx >= x-1 && cx <= x+1 && cy >= y-1 && cy <= y+1) {
                                    map[ncurrent].type = EMPTY;
                                } else if (cx >= x-4 && cx <= x+4 && cy >= y-2 && cy <= y+2) {
                                    map[ncurrent].type = FIRE;
                                } else if (map[ncurrent].type != EMPTY) {
                                    int nx = cx + sign(cx-x) * (rand() % (abs(x-cx)+4));
                                    int ny = cy + sign(cy-y) * (rand() % (abs(y-cy)+4));
                                    if (nx >= 0 && nx <= (MAPW-1) && ny >= 0 && ny <= (MAPH-1)) {
                                        int idx = (ny) * MAPW + (nx);
                                        map[idx].type = map[ncurrent].type;
                                        map[idx].timer = 0;
                                        map[ncurrent].type = FIRE;
                                    }
                                }
                                map[ncurrent].timer = 0;
                                if (cy >= y)
                                    map[ncurrent].skip_update = true;
                            }
                        }
                    }
                    map[current].type = EMPTY;
                    map[current].timer = 0;
                } else {
                    map[current].timer++;
                }
                break;
            }
        }
    }

    mtx_unlock(&map_mtx);
}

typedef struct {
    Cursor *cr; // Указатель на структуру курсора
    Cell *mp; // Указатель на массив ячеек
} InputThreadArgs;

bool run = true;

// Функция обработки ввода в отдельном потоке
int input_thread_loop(void *args) {
    Cursor *curs = ((InputThreadArgs *)args)->cr;
    Cell *map = ((InputThreadArgs *)args)->mp;

    bool button1 = false; // Нажата ли ЛКМ
    bool button3 = false; // Нажато ли колёсико мыши

    do {
        int c;
        MEVENT event;
        switch (c = getch()) {
        case 'q':
            run = false;
            break;
        case KEY_UP:
            if (curs->y > 0) {
                mtx_lock(&curs_mtx);
                curs->y--;
                mtx_unlock(&curs_mtx);
            }
            break;
        case KEY_DOWN:
            if (curs->y < (MAPH-1)) {
                mtx_lock(&curs_mtx);
                curs->y++;
                mtx_unlock(&curs_mtx);
            }
            break;
        case KEY_RIGHT:
            if (curs->x < (MAPW-1)) {
                mtx_lock(&curs_mtx);
                curs->x++;
                mtx_unlock(&curs_mtx);
            }
            break;
        case KEY_LEFT:
            if (curs->x > 0) {
                mtx_lock(&curs_mtx);
                curs->x--;
                mtx_unlock(&curs_mtx);
            }
            break;
        case KEY_MOUSE:
            if (getmouse(&event) == OK) {
                mtx_lock(&curs_mtx);
                if (event.x > 0 && event.x < MAPW+1) curs->x = event.x-1;
                if (event.y > 0 && event.y < MAPH+1) curs->y = event.y-1;

                if (event.bstate == BUTTON1_PRESSED) {
                    button1 = true;
                } else if (event.bstate == BUTTON1_RELEASED) {
                    button1 = false;
                } else if (event.bstate == BUTTON2_PRESSED) {
                    button3 = true;
                } else if (event.bstate == BUTTON2_RELEASED) {
                    button3 = false;
                } else if (event.bstate == BUTTON4_PRESSED) {
                    if (curs->brush_size < 99) curs->brush_size++;
                } else if (event.bstate == BUTTON5_PRESSED) {
                    if (curs->brush_size > 1) curs->brush_size--;
                }
                mtx_unlock(&curs_mtx);
            }
            break;
        case ' ':
            mtx_lock(&map_mtx);
            map[curs->y*MAPW + curs->x].type = curs->brush;
            mtx_unlock(&map_mtx);
            break;
        case 'c':
            mtx_lock(&map_mtx);
            memset(map, EMPTY, sizeof(Cell) * MAPSIZE);
            mtx_unlock(&map_mtx);
            break;
        case '+':
            if (curs->brush_size < 99) {
                mtx_lock(&curs_mtx);
                curs->brush_size++;
                mtx_unlock(&curs_mtx);
            }
            break;
        case '-':
            if (curs->brush_size > 1) {
                mtx_lock(&curs_mtx);
                curs->brush_size--;
                mtx_unlock(&curs_mtx);
            }
            break;
        default:
            if (isdigit(c)) {
                mtx_lock(&curs_mtx);
                curs->brush = c - '0';
                mtx_unlock(&curs_mtx);
            }
            break;
        }
        if (button1 || button3) {
            int y = curs->y-(curs->brush_size/2);
            if (y < 0) y = 0;

            mtx_lock(&map_mtx);
            for (; y <= curs->y+(curs->brush_size/2) && y <= (MAPH-1); y++) {
                int x = curs->x-curs->brush_size+1;
                if (x < 0) x = 0;
                for (; x <= curs->x+curs->brush_size-1 && x <= (MAPW-1); x++) {
                    map[y*MAPW + x].type = (button1 ? curs->brush : EMPTY);
                }
            }
            mtx_unlock(&map_mtx);
        }
        
    } while (run);

    return 0;
}

int main(int argc, char *argv[]) {
    char *prog = argv[0];
    int target_tps = DEFAULT_TARGET_TPS;
    if (argc > 1) {
        target_tps = atoi(argv[1]);
    }

    if (!initscr()) {
        fprintf(stderr, "%s: error initialising ncurses", prog);
        return 1;
    }
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);
    printf("\033[?1002h\n");
    refresh();

    WINDOW *win = newwin(LINES, COLS, 0, 0);
    box(win, 0, 0);
    wrefresh(win);
    
    start_color();
    init_color(COLOR_GRAY, 550, 550, 550);
    init_color(COLOR_DARKGRAY, 200, 200, 200);
    init_color(COLOR_BROWN, 400, 200, 0);
    init_color(COLOR_ORANGE, 1000, 500, 0);
    init_color(COLOR_BLACK, 0, 0, 0);

    init_pair(EMPTY, COLOR_WHITE, COLOR_BLACK);
    init_pair(SAND, COLOR_WHITE, COLOR_YELLOW);
    init_pair(WATER, COLOR_WHITE, COLOR_BLUE);
    init_pair(STONE, COLOR_BLACK, COLOR_DARKGRAY);
    init_pair(WOOD, COLOR_WHITE, COLOR_BROWN);
    init_pair(ASH, COLOR_WHITE, COLOR_GRAY);
    init_pair(FIRE, COLOR_WHITE, COLOR_RED);
    init_pair(FIRE+10, COLOR_WHITE, COLOR_ORANGE);
    init_pair(BOMB, COLOR_WHITE, COLOR_GREEN);
    init_pair(CURSOR_ID, COLOR_BLACK, COLOR_WHITE);

    CellInfo cells_info[] = {
        {" ", "Empty", (short[]){EMPTY}},
        {"#", "Sand", (short []){SAND}},
        {".", "Water", (short []){WATER}},
        {"@", "Stone", (short []){STONE}},
        {"$", "Wood", (short []){WOOD}},
        {"+", "Ash", (short []){ASH}},
        {"^!", "Fire", (short []){FIRE, FIRE+10}},
        {"&", "Bomb", (short []){BOMB}},
    };

    wattron(win, COLOR_PAIR(EMPTY));

    Cell map[MAPSIZE];
    memset(map, EMPTY, sizeof(Cell) * MAPSIZE);
    for (int y = 4; y < 14; y++) {
        for (int x = 20; x < 100; x++) {
            map[y*MAPW + x].type = STONE;
        }
    }
    for (int i = 0; i < MAPSIZE; i++) {
        if (rand() % 2)
            map[i].type = EMPTY;
        else
            map[i].type = SAND;
    }
    

    Cursor curs = {0, 2, .brush = SAND, .brush_size=1};

    mtx_init(&curs_mtx, mtx_plain);
    mtx_init(&map_mtx, mtx_plain);

    InputThreadArgs input_thrd_args = {.cr = &curs, .mp = map};
    thrd_t input_thrd;
    thrd_create(&input_thrd, input_thread_loop, &input_thrd_args);

    do {
        clock_t start_clock = clock();

        update(map);
        render(win, map, curs, cells_info, NULL);

        struct timespec delay = {0, (NS / target_tps) - ((clock()-start_clock) / (CLOCKS_PER_SEC * NS))};
        if (target_tps == 1) {
            delay.tv_nsec = 0;
            delay.tv_sec = 1;
        }
        nanosleep(&delay, NULL);
        
    } while (run);

    printf("\033[?1003l\n");
    curs_set(1);
    delwin(win);
    endwin();

    return 0;
}

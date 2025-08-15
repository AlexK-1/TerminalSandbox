#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <ncurses.h>
#include <pthread.h>

#define NS 1000000000L // Количество наносекунд в секунде
#define DEFAULT_TARGET_TPS 30
#define CURSOR_ID 10 // Номер цветовой пары для курсора
#define CURSOR_SPRITE '*' // Символ курсора, если будет пробел на карте на месте курсора
#define NUM_CELL_TYPES 8 // Количество типов клеток
#define CS_SPACES 4 // Сколько пробелов будет между названиями ячеек в меню

#define MAPW (COLS-2) // Ширина поля с ячейками на основе размера терминала
#define MAPH (LINES-2) // Высота поля с ячейками на основе размера терминала

#define random() (rand() % 100 / 100.0f)
#define sign(x) (x < 0 ? -1 : 1)
#define canmove(cellidx) (map.cells[cellidx].type == EMPTY || map.cells[cellidx].type == WATER) // Проверка клетки на то, что она нетвёрдая
#define canmover(cellidx, x) (canmove(cellidx) && (x < map.width-1)) // Проверка клетки на то, что она нетвёрдая, и проверка на границы справа
#define canmovel(cellidx, x) (canmove(cellidx) && (x > 0)) // Проверка клетки на то, что она нетвёрдая, и проверка на границы слева
#define watercanmove(cellidx) (map.cells[cellidx].type == EMPTY) // Проверка на то, может ли в клетку перейти вода
#define watercanmover(cellidx, x) (watercanmove(cellidx) && (x < map.width-1)) // Проверка на то, может ли в клетку перейти вода, границы справа
#define watercanmovel(cellidx, x) (watercanmove(cellidx) && (x > 0)) // Проверка на то, может ли в клетку перейти вода, границы слева

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
    Cell *cells;
    unsigned short width, height;
} CellsMap;

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

pthread_mutex_t map_mtx;
pthread_mutex_t curs_mtx;

void render(WINDOW *window, CellsMap map, Cursor cursor, CellInfo cells_info[]) {
    pthread_mutex_lock(&map_mtx);
    pthread_mutex_lock(&curs_mtx);
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            int current = y*map.width + x;
            if (map.cells[current].skip_render) {
                map.cells[current].skip_render = false;
                continue;
            }
            wmove(window, y+1, x+1);
            CellInfo current_cell_info = cells_info[map.cells[current].type];
            if (x >= cursor.x-cursor.brush_size+1 && x <= cursor.x+cursor.brush_size-1 && \
                y >= cursor.y-(cursor.brush_size/2) && y <= cursor.y+(cursor.brush_size/2)) {
                if (map.cells[current].type == EMPTY) {
                    waddch(window, CURSOR_SPRITE | COLOR_PAIR(CURSOR_ID));
                } else {
                    waddch(window, current_cell_info.sprites[0] | COLOR_PAIR(CURSOR_ID));
                }
            } else {
                waddch(window, current_cell_info.sprites[0] | A_PROTECT | COLOR_PAIR(current_cell_info.colors[0]));

                if (map.cells[current].type == FIRE) {
                    //int neighbors[8] = {current-map.width-1, current-map.width, current-map.width+1, current-1, current+1, current+map.width-1, current+map.width, current+map.width+1};
                    int neighbors_x[8] = {x-1, x, x+1, x-1, x+1, x-1, x, x+1};
                    int neighbors_y[8] = {y-1, y-1, y-1, y, y, y+1, y+1, y+1};
                    
                    for (int i = 0; i < 8; i++) {
                        if (neighbors_x[i] >= 0 && neighbors_x[i] <= (map.width-1) && neighbors_y[i] >= 0 && neighbors_y[i] <= (map.height-1) && (rand() % 10 < 3)) {
                            map.cells[neighbors_y[i]*map.width + neighbors_x[i]].skip_render = true;
                            wmove(window, neighbors_y[i]+1, neighbors_x[i]+1);
                            waddch(window, cells_info[FIRE].sprites[rand() % 2] | COLOR_PAIR(cells_info[FIRE].colors[rand() % 2]));
                        }
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&map_mtx);

    CellInfo brush_info = cells_info[cursor.brush];

    wmove(window, 1, 1);
    waddch(window, brush_info.sprites[0] | COLOR_PAIR(brush_info.colors[0]));

    char brushsize_str[3];
    sprintf(brushsize_str, "%d", (cursor.brush_size <= 99 ? cursor.brush_size : 99));
    pthread_mutex_unlock(&curs_mtx);
    wmove(window, 1, 3);
    waddstr(window, brushsize_str);

    wmove(window, 1, 6);
    waddstr(window, brush_info.name);

    wrefresh(window);
}

void update(CellsMap map) {
    int order[map.width]; // Массив с порядком обработки ячеек
    for (int i = 0; i < map.width; i++)
        order[i] = i;
    shuffle(order, map.width); // Перемешивание массива, чтобы ячейки обрабатывались в случайном порядке
    
    pthread_mutex_lock(&map_mtx);

    for (int y = map.height-1; y >= 0; y--) { // y - координата ячейки по Y
        rotate(order, map.width, rand() % map.width);
        for (int i = 0; i < map.width; i++) {
            int x = order[i]; // Координата ячейки по x
            int current = y*map.width + x;
            if (map.cells[current].skip_update) {
                map.cells[current].skip_update = false;
                continue;
            }
            int top = (y-1)*map.width + x;
            int bottom = (y+1)*map.width + x;
            Cell t;

            int movements[8] = {0}; // Массив индексов клеток, куда можно переместиться
            int j = 0;

            switch (map.cells[current].type) {
            case EMPTY:
            case WOOD:
            case STONE:
                break;
            case ASH:
            case SAND:
                if (y == map.height-1) {
                    break;
                }
               if (canmove(bottom)) {
                    swap(map.cells[current], map.cells[bottom], t);
                } else if (canmovel(bottom - 1, x) && canmover(bottom + 1, x)) {
                    int idx = bottom + (rand() % 2 ? 1 : -1);
                    swap(map.cells[current], map.cells[idx], t);
                    //swap(map[current], map[bottom + (rand() % 2 ? 1 : -1)], t);
                } else if (canmovel(bottom - 1, x) && !canmover(bottom + 1, x)) {
                    swap(map.cells[current], map.cells[bottom - 1], t);
                } else if (!canmovel(bottom - 1, x) && canmover(bottom + 1, x)) {
                    swap(map.cells[current], map.cells[bottom + 1], t);
                }
                break;
            case WATER:
                if (y < map.height-1 && watercanmove(bottom)) {
                    swap(map.cells[current], map.cells[bottom], t);
                } else if (watercanmovel(current - 1, x) && watercanmover(current + 1, x) && (y == map.height-1 || (!watercanmovel(bottom - 1, x) && !watercanmover(bottom + 1, x)))) {
                    int idx = current + (rand() % 2 ? 1 : -1);
                    swap(map.cells[current], map.cells[idx], t);
                    //swap(map.cells[current], map.cells[current + (rand() % 2 ? 1 : -1)], t);
                } else if (watercanmovel(current - 1, x) && !watercanmover(current + 1, x) && (y == map.height-1 || (!watercanmovel(bottom - 1, x) && !watercanmover(bottom + 1, x)))) {
                    swap(map.cells[current], map.cells[current - 1], t);
                } else if (!watercanmovel(current - 1, x) && watercanmover(current + 1, x) && (y == map.height-1 || (!watercanmovel(bottom - 1, x) && !watercanmover(bottom + 1, x)))) {
                    swap(map.cells[current], map.cells[current + 1], t);
                } else if (y < map.height-1 && watercanmovel(bottom - 1, x) && watercanmover(bottom + 1, x)) {
                    int idx = bottom + (rand() % 2 ? 1 : -1);
                    swap(map.cells[current], map.cells[idx], t);
                    //swap(map.cells[current], map.cells[bottom + (rand() % 2 ? 1 : -1)], t);
                } else if (y < map.height-1 && watercanmovel(bottom - 1, x) && !watercanmover(bottom + 1, x)) {
                    swap(map.cells[current], map.cells[bottom - 1], t);
                } else if (y < map.height-1 && !watercanmovel(bottom - 1, x) && watercanmover(bottom + 1, x)) {
                    swap(map.cells[current], map.cells[bottom + 1], t);
                }
                break;
            case FIRE:
                if (y > 0) {
                    if (map.cells[top].type == WATER) goto fireclear;
                    else if (map.cells[top].type == WOOD) movements[j++] = top;
                    if (x > 0 && map.cells[top - 1].type == WATER) goto fireclear;
                    else if (x > 0 && map.cells[top - 1].type == WOOD) movements[j++] = top-1;
                    if (x < map.width-1 && map.cells[top + 1].type == WATER) goto fireclear;
                    else if (x < map.width-1 && map.cells[top + 1].type == WOOD) movements[j++] = top+1;
                }

                if (x > 0 && map.cells[current - 1].type == WATER) goto fireclear;
                else if (x > 0 && map.cells[current - 1].type == WOOD) movements[j++] = current-1;
                if (x < map.width-1 && map.cells[current + 1].type == WATER) goto fireclear;
                else if (x < map.width-1 && map.cells[current + 1].type == WOOD) movements[j++] = current+1;
                
                if (y < map.height-1) {
                    if (map.cells[bottom].type == WATER) goto fireclear;
                    else if (map.cells[bottom].type == WOOD) movements[j++] = bottom;
                    if (x > 0 && map.cells[bottom - 1].type == WATER) goto fireclear;
                    else if (x > 0 && map.cells[bottom - 1].type == WOOD) movements[j++] = bottom - 1;
                    if (x < map.width-1 && map.cells[bottom + 1].type == WATER) goto fireclear;
                    else if (x < map.width-1 && map.cells[bottom + 1].type == WOOD) movements[j++] = bottom + 1;
                }
                if (j > 0) {
                    float r = random();

                    if (r > 0.15f)
                        break;

                    j *= random(); // Случайное сичло в диапазоне 0..j
                    map.cells[movements[j]].type = FIRE;
                    if (movements[j] > bottom-1) // Пропуск обновления новой ячейки огня, если она будет ещё раз обрабатываться в цикле за этот кадр
                        map.cells[movements[j]].skip_update = true;
                    if (r < 0.06f) {
                        map.cells[current].type = (r < 0.04f) ? ASH : FIRE;
                    } else {
                        map.cells[current].type = EMPTY;
                    }
                } else {
                    fireclear:
                    map.cells[current].type = EMPTY;
                }
                break;
            case BOMB:
                if (y > 0) {
                    if (map.cells[top].type == FIRE) goto boom;
                    if (x > 0 && map.cells[top - 1].type == FIRE) goto boom;
                    if (x < map.width-1 && map.cells[top + 1].type == FIRE) goto boom;
                }
                if (x > 0 && map.cells[current - 1].type == FIRE) goto boom;
                if (x < map.width-1 && map.cells[current + 1].type == FIRE) goto boom;
                if (y < map.height-1) {
                    if (map.cells[bottom].type == FIRE) goto boom;
                    if (x > 0 && map.cells[bottom - 1].type == FIRE) goto boom;
                    if (x < map.width-1 && map.cells[bottom + 1].type == FIRE) goto boom;
                }
                if (map.cells[current].timer >= 50) {
                    boom:
                    for (int cy = y-3; cy <= y+3; cy++) {
                        for (int cx = x-7; cx <= x+7; cx++) {
                            if (cx >= 0 && cx <= (map.width-1) && cy >= 0 && cy <= (map.height-1)) {
                                if (rand() % 6 == 0) continue;
                                
                                int ncurrent = cy*map.width + cx;

                                if (cx >= x-1 && cx <= x+1 && cy >= y-1 && cy <= y+1) {
                                    map.cells[ncurrent].type = EMPTY;
                                } else if (cx >= x-4 && cx <= x+4 && cy >= y-2 && cy <= y+2) {
                                    map.cells[ncurrent].type = FIRE;
                                } else if (map.cells[ncurrent].type != EMPTY) {
                                    int nx = cx + sign(cx-x) * (rand() % (abs(x-cx)+4));
                                    int ny = cy + sign(cy-y) * (rand() % (abs(y-cy)+4));
                                    if (nx >= 0 && nx <= (map.width-1) && ny >= 0 && ny <= (map.height-1)) {
                                        int idx = (ny) * map.width + (nx);
                                        map.cells[idx].type = map.cells[ncurrent].type;
                                        map.cells[idx].timer = 0;
                                        map.cells[ncurrent].type = FIRE;
                                    }
                                }
                                map.cells[ncurrent].timer = 0;
                                if (cy >= y)
                                    map.cells[ncurrent].skip_update = true;
                            }
                        }
                    }
                    map.cells[current].type = EMPTY;
                    map.cells[current].timer = 0;
                } else {
                    map.cells[current].timer++;
                }
                break;
            }
        }
    }

    pthread_mutex_unlock(&map_mtx);
}

typedef struct {
    Cursor *cr; // Указатель на структуру курсора
    CellsMap mp; // Структура с массивом ячеек
} InputThreadArgs;

bool run = true;
bool cellselect_open = false;
pthread_mutex_t cellselect_mtx;
pthread_cond_t cellselect_cnd;

// Функция обработки ввода в отдельном потоке
void *input_thread_loop(void *args) {
    Cursor *curs = ((InputThreadArgs *)args)->cr;
    CellsMap map = ((InputThreadArgs *)args)->mp;

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
                pthread_mutex_lock(&curs_mtx);
                curs->y--;
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        case KEY_DOWN:
            if (curs->y < (map.height-1)) {
                pthread_mutex_lock(&curs_mtx);
                curs->y++;
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        case KEY_RIGHT:
            if (curs->x < (map.width-1)) {
                pthread_mutex_lock(&curs_mtx);
                curs->x++;
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        case KEY_LEFT:
            if (curs->x > 0) {
                pthread_mutex_lock(&curs_mtx);
                curs->x--;
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        case KEY_MOUSE:
            if (getmouse(&event) == OK) {
                pthread_mutex_lock(&curs_mtx);
                if (event.x > 0 && event.x < map.width+1) curs->x = event.x-1;
                if (event.y > 0 && event.y < map.height+1) curs->y = event.y-1;

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
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        case ' ':
            pthread_mutex_lock(&map_mtx);
            map.cells[curs->y*map.width + curs->x].type = curs->brush;
            pthread_mutex_unlock(&map_mtx);
            break;
        case 'c':
            pthread_mutex_lock(&map_mtx);
            memset(map.cells, EMPTY, sizeof(Cell) * map.width*map.height);
            pthread_mutex_unlock(&map_mtx);
            break;
        case '+':
            if (curs->brush_size < 99) {
                pthread_mutex_lock(&curs_mtx);
                curs->brush_size++;
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        case '-':
            if (curs->brush_size > 1) {
                pthread_mutex_lock(&curs_mtx);
                curs->brush_size--;
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        case '\t':
            cellselect_open = true;
            pthread_mutex_lock(&cellselect_mtx);
            while (cellselect_open) {
                pthread_cond_wait(&cellselect_cnd, &cellselect_mtx);
            }
            pthread_mutex_unlock(&cellselect_mtx);
            break;
        default:
            if (isdigit(c) && (c-'0' < NUM_CELL_TYPES)) {
                pthread_mutex_lock(&curs_mtx);
                curs->brush = c - '0';
                pthread_mutex_unlock(&curs_mtx);
            }
            break;
        }
        if (button1 || button3) {
            int y = curs->y-(curs->brush_size/2);
            if (y < 0) y = 0;

            pthread_mutex_lock(&map_mtx);
            for (; y <= curs->y+(curs->brush_size/2) && y <= (map.height-1); y++) {
                int x = curs->x-curs->brush_size+1;
                if (x < 0) x = 0;
                for (; x <= curs->x+curs->brush_size-1 && x <= (map.width-1); x++) {
                    map.cells[y*map.width + x].type = (button1 ? curs->brush : EMPTY);
                }
            }
            pthread_mutex_unlock(&map_mtx);
        }
        
    } while (run);

    return NULL;
}

int main(int argc, char *argv[]) {
    char *prog = argv[0];
    int target_tps = DEFAULT_TARGET_TPS;
    if (argc > 1) {
        target_tps = atoi(argv[1]);
    }

    if (!initscr()) {
        fprintf(stderr, "%s: error initialising ncurses\n", prog);
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

    CellsMap map = {.cells = NULL, .width = COLS-2, .height = LINES-2};
    map.cells = calloc(MAPW*MAPH, sizeof(Cell));
    if (map.cells == NULL) {
        fprintf(stderr, "%s: error allocating memory\n", prog);

        printf("\033[?1003l\n");
        curs_set(1);
        delwin(win);
        endwin();

        return 1;
    }
    //memset(map, EMPTY, sizeof(Cell) * MAPSIZE);
    /*for (int y = 4; y < 14; y++) {
        for (int x = 20; x < 100; x++) {
            map.cells[y*MAPW + x].type = STONE;
        }
    }*/
    for (int i = 0; i < map.width*map.height; i++) {
        if (rand() % 2)
            map.cells[i].type = EMPTY;
        else
            map.cells[i].type = SAND;
    }
    

    Cursor curs = {0, 2, .brush = SAND, .brush_size=1};

    pthread_mutex_init(&curs_mtx, NULL);
    pthread_mutex_init(&map_mtx, NULL);
    pthread_mutex_init(&cellselect_mtx, NULL);
    pthread_cond_init(&cellselect_cnd, NULL);

    InputThreadArgs input_thrd_args = {.cr = &curs, .mp = map};
    pthread_t input_thrd;
    pthread_create(&input_thrd, NULL, input_thread_loop, &input_thrd_args);
    pthread_detach(input_thrd);

    do {
        if (cellselect_open) {
            // cs = cellselect
            int cs_win_height = map.height/1.5;
            int cs_win_width = map.width/1.5;
            int cs_win_x = map.width/6;
            int cs_win_y = map.height/6;

            WINDOW *cs_win = newwin(cs_win_height, cs_win_width, cs_win_y, cs_win_x);
            nodelay(cs_win, TRUE);
            keypad(cs_win, TRUE);
            
            box(cs_win, 0, 0);
            wmove(cs_win, 0, 2);
            waddstr(cs_win, "Cells menu");
            wrefresh(cs_win);

            //thrd_sleep(&(struct timespec){.tv_sec = 3}, NULL);

            bool first_run = true;
            int c;
            while ((c = getch()) != '\t') {
                if (first_run == true)
                    first_run = false;
                else if (c == EOF)
                    continue;
                else if (c == 'q') {
                    run = false;
                    break;
                } else if (c == KEY_RIGHT) {
                    if (curs.brush < NUM_CELL_TYPES-1) curs.brush++;
                } else if (c == KEY_LEFT) {
                    if (curs.brush > 0) curs.brush--;
                } else if (c == KEY_MOUSE) {
                    MEVENT event;
                    if (getmouse(&event) == OK && event.bstate == BUTTON1_PRESSED
                                               && event.x >= cs_win_x+2 && event.x <= cs_win_x+cs_win_width-2 \
                                               && event.y >= cs_win_y+1 && event.y <= cs_win_y+cs_win_height-1) {
                        int click_x = event.x - cs_win_x;
                        int click_y = event.y - cs_win_y;

                        int line_num = 0;
                        int line_len = 0;
                        for (int i = 0; i < NUM_CELL_TYPES; i++) {
                            int cellname_len = strlen(cells_info[i].name);
                            if (line_len + cellname_len + 2 + CS_SPACES > cs_win_width-4) {
                                line_num += 2;
                                line_len = 0;
                            }
                            line_len += cellname_len + (line_len == 0 ? 0 : CS_SPACES) + 2;

                            if (line_num == click_y-1 && line_len > click_x-2) {
                                if (line_len - (click_x-2) < cellname_len+3) {
                                    curs.brush = i;
                                }
                                break;
                            }
                        }
                    }
                }
                
                int line_num = 0;
                int line_len = 0;
                for (int i = 0; i < NUM_CELL_TYPES; i++) {
                    int cellname_len = strlen(cells_info[i].name);
                    if (line_len + cellname_len + 2 + CS_SPACES > cs_win_width-4) {
                        line_num++;
                        line_len = 0;
                    }
                    wmove(cs_win, line_num*2 + 1, (line_len == 0 ? 0 : line_len+CS_SPACES) + 2);
                    waddch(cs_win, cells_info[i].sprites[0] | COLOR_PAIR(cells_info[i].colors[0]));
                    if ((CellType)i == curs.brush) {
                        waddch(cs_win, ' ' | COLOR_PAIR(CURSOR_ID) | A_PROTECT);
                    } else {
                        waddch(cs_win, ' ' | COLOR_PAIR(EMPTY) | A_PROTECT);
                    }
                    if ((CellType)i == curs.brush) {
                        wattron(cs_win, COLOR_PAIR(CURSOR_ID));
                        waddstr(cs_win, cells_info[i].name);
                        wattroff(cs_win, COLOR_PAIR(CURSOR_ID));
                    } else {
                        waddstr(cs_win, cells_info[i].name);
                    }
                    line_len += cellname_len + (line_len == 0 ? 0 : CS_SPACES) + 2;
                }

                CellInfo brush_info = cells_info[curs.brush];

                wmove(win, 1, 1);
                waddch(win, brush_info.sprites[0] | COLOR_PAIR(brush_info.colors[0]));

                wmove(win, 1, 6);
                waddstr(win, brush_info.name);
                for (int i = 0; i < (10-(int)strlen(brush_info.name)); i++)
                    waddch(win, ' ');

                wrefresh(win);
                wrefresh(cs_win);
            }

            delwin(cs_win);

            pthread_mutex_lock(&cellselect_mtx);
            cellselect_open = false;
            pthread_cond_signal(&cellselect_cnd);
            pthread_mutex_unlock(&cellselect_mtx);
        }

        clock_t start_clock = clock();

        update(map);
        render(win, map, curs, cells_info);

        if (target_tps > 0) {
            struct timespec delay = {0, 0};
            if (target_tps == 1) {
                delay.tv_sec = 1;
            } else {
                delay.tv_nsec = (NS / target_tps) - ((clock()-start_clock) / (CLOCKS_PER_SEC * NS));
            }

            nanosleep(&delay, NULL);
        }
    } while (run);

    pthread_mutex_destroy(&map_mtx);
    pthread_mutex_destroy(&curs_mtx);
    pthread_mutex_destroy(&cellselect_mtx);
    pthread_cond_destroy(&cellselect_cnd);

    printf("\033[?1003l\n");
    curs_set(1);
    delwin(win);
    endwin();

    free(map.cells);

    return 0;
}

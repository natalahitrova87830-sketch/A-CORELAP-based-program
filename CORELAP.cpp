#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <queue>
#include <chrono>
#include <iomanip>
#include <limits>
#include <string>
#include <vector>
#include <random>
#include <ctime>
#include <cstring>

using namespace std::chrono;

// ==================== КОНСТАНТЫ ====================

constexpr int MAX_DEPS = 100;
constexpr int MAX_CATEGORIES = 5;
constexpr double EPSILON = 0.001;
constexpr int MAX_FIX_ITERATIONS = 30;
constexpr double HTML_SCALE = 30.0;
constexpr int MIN_BLOCK_W = 60;
constexpr int MIN_BLOCK_H = 40;
constexpr int CATEGORY_SCORES[] = { 16, 8, 4, 2, 0 };

// ==================== СТРУКТУРЫ ====================

struct Point { double x = 0, y = 0; };

enum FlowCategory { VERY_IMPORTANT = 0, IMPORTANT = 1, MEDIUM = 2, UNIMPORTANT = 3, VERY_UNIMPORTANT = 4 };

constexpr const char* CATEGORY_NAMES[] = {
    "Очень важный", "Важный", "Средний", "Неважный", "Совсем неважный"
};

constexpr const char* CATEGORY_COLORS[] = {
    "#c0392b", "#d35400", "#f39c12", "#27ae60", "#7f8c8d"
};

struct Department {
    int id;
    std::string name;
    double area = 0;
    double width = 0, length = 0;
    double total_flow = 0;
    int total_score = 0;
    FlowCategory category = VERY_UNIMPORTANT;
    bool placed = false;
    Point center;
    std::vector<std::pair<int, int>> position_history;
};

struct Building {
    double span_width = 12;
    int span_count = 3;
    double max_length = 60;
};

// ==================== ПРЕДВАРИТЕЛЬНЫЕ ОБЪЯВЛЕНИЯ ====================

double calc_total_flow();
void draw_html(const Building& b);
void interactive_rearrange(const Building& b);
void fix_overlaps();
bool can_place(int row, int col, int length);
void place_dep(int idx, int row, int col, int length);
void remove_dep(int idx);
int get_row(const Department& d);
double manhattan(const Point& a, const Point& b);
void show_placed();
void show_unplaced();
void update_grid_from_departments();

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================

Department deps[MAX_DEPS];
Department saved_deps[MAX_DEPS];
int dep_count = 0, saved_dep_count = 0;

double flow_sum[MAX_DEPS][MAX_DEPS];
double flow_original[MAX_DEPS][MAX_DEPS];
int score_matrix[MAX_DEPS][MAX_DEPS];
double saved_flow_sum[MAX_DEPS][MAX_DEPS];
int saved_score_matrix[MAX_DEPS][MAX_DEPS];

Building saved_building;
bool has_saved_data = false;

int* grid = nullptr;
int grid_rows = 0, grid_cols = 0;

double min_flow = 0, max_flow = 0, cat_step = 0;
int run_number = 0;

std::mt19937 rng(static_cast<unsigned>(time(nullptr)));

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

inline void clear_input() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

int read_int(const char* prompt, int min_val = 0, int max_val = INT_MAX) {
    int value;
    while (true) {
        std::cout << prompt;
        if (std::cin >> value && value >= min_val && value <= max_val) {
            clear_input();
            return value;
        }
        std::cout << "Ошибка! Введите целое число от " << min_val << " до " << max_val << ".\n";
        clear_input();
    }
}

double read_double(const char* prompt, double min_val = 0.0, double max_val = 1e9) {
    double value;
    while (true) {
        std::cout << prompt;
        if (std::cin >> value && value >= min_val && value <= max_val) {
            clear_input();
            return value;
        }
        std::cout << "Ошибка! Введите число от " << min_val << " до " << max_val << ".\n";
        clear_input();
    }
}

int read_menu_choice(const char* prompt, int min_val, int max_val) {
    int value;
    while (true) {
        std::cout << prompt;
        if (std::cin >> value && value >= min_val && value <= max_val) {
            clear_input();
            return value;
        }
        std::cout << "Ошибка! Выберите от " << min_val << " до " << max_val << ".\n";
        clear_input();
    }
}

inline double manhattan(const Point& a, const Point& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

inline int get_grid(int r, int c) {
    return (r >= 0 && r < grid_rows && c >= 0 && c < grid_cols) ? grid[r * grid_cols + c] : -1;
}

inline void set_grid(int r, int c, int val) {
    if (r >= 0 && r < grid_rows && c >= 0 && c < grid_cols) grid[r * grid_cols + c] = val;
}

inline int get_row(const Department& d) {
    if (d.width <= 0) return 0;
    return (std::max)(0, (std::min)(static_cast<int>(d.center.y / d.width), grid_rows - 1));
}

inline int clamp_int(int v, int lo, int hi) {
    return (std::max)(lo, (std::min)(v, hi));
}

inline int get_department_length(int idx) {
    return clamp_int(static_cast<int>(deps[idx].length), 1, grid_cols);
}

// Обновление сетки из данных подразделений
void update_grid_from_departments() {
    // Очищаем сетку
    for (int i = 0; i < grid_rows * grid_cols; ++i) grid[i] = 0;

    // Заполняем сетку заново
    for (int i = 0; i < dep_count; ++i) {
        if (deps[i].placed) {
            int row = get_row(deps[i]);
            int col = static_cast<int>(deps[i].center.x - deps[i].length * 0.5);
            int len = get_department_length(i);
            for (int c = col; c < col + len; ++c) {
                if (c >= 0 && c < grid_cols && row >= 0 && row < grid_rows) {
                    set_grid(row, c, deps[i].id);
                }
            }
        }
    }
}

// ==================== ПРОВЕРКА ПЕРЕСЕЧЕНИЙ ====================

bool would_overlap(int row, int col, int length, int exclude_id = -1) {
    double new_left = col, new_right = col + length;
    for (int i = 0; i < dep_count; ++i) {
        if (!deps[i].placed || deps[i].id == exclude_id) continue;
        if (get_row(deps[i]) != row) continue;
        double d_left = deps[i].center.x - deps[i].length * 0.5;
        double d_right = deps[i].center.x + deps[i].length * 0.5;
        if (new_left < d_right && new_right > d_left) return true;
    }
    return false;
}

bool can_place(int row, int col, int length) {
    if (row < 0 || row >= grid_rows || col < 0 || col + length > grid_cols) return false;
    for (int c = col; c < col + length; ++c) if (get_grid(row, c)) return false;
    return !would_overlap(row, col, length);
}

// ==================== УПРАВЛЕНИЕ СЕТКОЙ ====================

void create_grid(const Building& b) {
    grid_rows = (std::max)(1, b.span_count);
    grid_cols = (std::max)(1, static_cast<int>(b.max_length));
    delete[] grid;
    grid = new int[grid_rows * grid_cols]();
}

void place_dep(int idx, int row, int col, int length) {
    row = clamp_int(row, 0, grid_rows - 1);
    col = clamp_int(col, 0, grid_cols - 1);
    length = clamp_int(length, 1, grid_cols - col);

    if (would_overlap(row, col, length, deps[idx].id)) {
        for (int r = 0; r < grid_rows; ++r)
            for (int c = 0; c <= grid_cols - length; ++c)
                if (!would_overlap(r, c, length, deps[idx].id) && !get_grid(r, c)) {
                    row = r; col = c; break;
                }
    }

    if (deps[idx].placed) {
        for (int r = 0; r < grid_rows; ++r)
            for (int c = 0; c < grid_cols; ++c)
                if (get_grid(r, c) == deps[idx].id) set_grid(r, c, 0);
    }

    deps[idx].position_history.push_back({ row, col });

    deps[idx].placed = true;
    deps[idx].center.x = col + length * 0.5;
    deps[idx].center.y = row * deps[idx].width + deps[idx].width * 0.5;
    for (int c = col; c < col + length; ++c) set_grid(row, c, deps[idx].id);
}

void remove_dep(int idx) {
    int id = deps[idx].id;
    for (int i = 0; i < grid_rows * grid_cols; ++i)
        if (grid[i] == id) grid[i] = 0;
    deps[idx].placed = false;
}

// ==================== ИСПРАВЛЕНИЕ НАЛОЖЕНИЙ ====================

void fix_overlaps() {
    for (int iter = 0; iter < MAX_FIX_ITERATIONS; ++iter) {
        bool fixed = false;
        for (int i = 0; i < dep_count; ++i) {
            if (!deps[i].placed) continue;
            for (int j = i + 1; j < dep_count; ++j) {
                if (!deps[j].placed) continue;
                if (get_row(deps[i]) != get_row(deps[j])) continue;

                double il = deps[i].center.x - deps[i].length * 0.5;
                double ir = deps[i].center.x + deps[i].length * 0.5;
                double jl = deps[j].center.x - deps[j].length * 0.5;
                double jr = deps[j].center.x + deps[j].length * 0.5;

                bool overlap = (il < jr && ir > jl) && (ir != jl && jr != il);
                bool contain = (il > jl && ir < jr) || (jl > il && jr < ir);

                if (overlap || contain) {
                    int mover = (deps[i].total_score <= deps[j].total_score) ? i : j;
                    int keeper = (mover == i) ? j : i;
                    int mlen = get_department_length(mover);
                    int row = get_row(deps[keeper]);
                    int new_col = clamp_int(static_cast<int>(deps[keeper].center.x + deps[keeper].length * 0.5 + deps[mover].length * 0.5 - deps[mover].length * 0.5), 0, grid_cols - mlen);

                    bool moved = false;
                    for (int c = new_col; c <= grid_cols - mlen && !moved; ++c)
                        if (can_place(row, c, mlen)) { remove_dep(mover); place_dep(mover, row, c, mlen); moved = true; }
                    if (!moved)
                        for (int r = 0; r < grid_rows && !moved; ++r)
                            for (int c = 0; c <= grid_cols - mlen && !moved; ++c)
                                if (can_place(r, c, mlen)) { remove_dep(mover); place_dep(mover, r, c, mlen); moved = true; }
                    if (moved) fixed = true;
                }
            }
        }
        if (!fixed) break;
    }
}

// ==================== АЛГОРИТМ ПЕРЕСТАНОВКИ ====================

bool shift_row(int row, int from_col, int needed) {
    if (row < 0 || row >= grid_rows) return false;
    bool shift_right = (rand() % 2 == 0);

    if (shift_right) {
        std::vector<int> ids;
        for (int c = from_col; c < grid_cols; ++c) {
            int v = get_grid(row, c);
            if (v && std::find(ids.begin(), ids.end(), v) == ids.end()) ids.push_back(v);
        }
        if (ids.empty()) return false;
        for (int id : ids) {
            int rm = -1;
            for (int c = grid_cols - 1; c >= 0; --c) if (get_grid(row, c) == id) { rm = c; break; }
            if (rm == -1 || rm + 1 >= grid_cols || get_grid(row, rm + 1)) return false;
        }
        for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
            int id = *it, idx = -1;
            for (int j = 0; j < dep_count; ++j) if (deps[j].id == id) { idx = j; break; }
            if (idx == -1) continue;
            int start = -1, len = 0;
            for (int c = 0; c < grid_cols; ++c) if (get_grid(row, c) == id) { if (start == -1) start = c; ++len; }
            for (int c = start; c < start + len; ++c) set_grid(row, c, 0);
            for (int c = start + 1; c < start + 1 + len; ++c) set_grid(row, c, id);
            deps[idx].center.x = start + 1 + len * 0.5;
        }
        return true;
    }
    else {
        std::vector<int> ids;
        for (int c = 0; c <= from_col + needed; ++c) {
            int v = get_grid(row, c);
            if (v && std::find(ids.begin(), ids.end(), v) == ids.end()) ids.push_back(v);
        }
        if (ids.empty()) return false;
        for (int id : ids) {
            int lm = grid_cols;
            for (int c = 0; c < grid_cols; ++c) if (get_grid(row, c) == id) { lm = c; break; }
            if (lm == grid_cols || lm - 1 < 0 || get_grid(row, lm - 1)) return false;
        }
        std::reverse(ids.begin(), ids.end());
        for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
            int id = *it, idx = -1;
            for (int j = 0; j < dep_count; ++j) if (deps[j].id == id) { idx = j; break; }
            if (idx == -1) continue;
            int start = -1, len = 0;
            for (int c = 0; c < grid_cols; ++c) if (get_grid(row, c) == id) { if (start == -1) start = c; ++len; }
            for (int c = start; c < start + len; ++c) set_grid(row, c, 0);
            for (int c = start - 1; c < start - 1 + len; ++c) set_grid(row, c, id);
            deps[idx].center.x = start - 1 + len * 0.5;
        }
        return true;
    }
}

bool move_last_to_history_position(int last_idx, int current_idx) {
    if (deps[last_idx].position_history.size() < 2) return false;

    deps[last_idx].position_history.pop_back();

    auto [old_row, old_col] = deps[last_idx].position_history.back();
    int len = get_department_length(last_idx);

    if (!can_place(old_row, old_col, len)) {
        deps[last_idx].position_history.push_back({ old_row, old_col });
        return false;
    }

    remove_dep(last_idx);
    deps[last_idx].position_history.pop_back();
    place_dep(last_idx, old_row, old_col, len);

    int clen = get_department_length(current_idx);
    for (int r = 0; r < grid_rows; ++r)
        for (int c = 0; c <= grid_cols - clen; ++c)
            if (can_place(r, c, clen)) {
                place_dep(current_idx, r, c, clen);
                std::cout << "  + " << deps[current_idx].name << " (после возврата " << deps[last_idx].name << " на старую позицию)\n";
                return true;
            }

    remove_dep(last_idx);
    place_dep(last_idx, old_row, old_col, len);
    deps[last_idx].position_history.push_back({ old_row, old_col });
    return false;
}

bool move_to_span(int cur, std::vector<int>& placed) {
    for (int idx : placed) {
        int orow = get_row(deps[idx]);
        int len = get_department_length(idx);
        for (int r = 0; r < grid_rows; ++r) {
            if (r == orow) continue;
            for (int c = 0; c <= grid_cols - len; ++c) {
                if (!can_place(r, c, len)) continue;
                remove_dep(idx); place_dep(idx, r, c, len);
                int clen = get_department_length(cur);
                for (int cr = 0; cr < grid_rows; ++cr)
                    for (int cc = 0; cc <= grid_cols - clen; ++cc)
                        if (can_place(cr, cc, clen)) { place_dep(cur, cr, cc, clen); return true; }
                remove_dep(idx);
                place_dep(idx, orow, static_cast<int>(deps[idx].center.x - deps[idx].length * 0.5), len);
                return false;
            }
        }
    }
    return false;
}

bool random_replace(int cur, std::vector<int>& placed, std::vector<int>& unplaced) {
    if (placed.empty() || unplaced.empty()) return false;
    std::shuffle(placed.begin(), placed.end(), rng);
    std::shuffle(unplaced.begin(), unplaced.end(), rng);
    for (int pi : placed) {
        for (int ui : unplaced) {
            int orow = get_row(deps[pi]);
            int ocol = (std::max)(0, static_cast<int>(deps[pi].center.x - deps[pi].length * 0.5));
            int plen = get_department_length(pi);
            int ulen = get_department_length(ui);
            int clen = get_department_length(cur);
            if (ulen > plen) continue;
            remove_dep(pi);
            if (!can_place(orow, ocol, ulen)) { place_dep(pi, orow, ocol, plen); continue; }
            place_dep(ui, orow, ocol, ulen);
            bool pc = false;
            for (int r = 0; r < grid_rows && !pc; ++r)
                for (int c = 0; c <= grid_cols - clen && !pc; ++c)
                    if (can_place(r, c, clen)) { place_dep(cur, r, c, clen); pc = true; }
            if (pc) {
                bool pr = false;
                for (int r = 0; r < grid_rows && !pr; ++r)
                    for (int c = 0; c <= grid_cols - plen && !pr; ++c)
                        if (can_place(r, c, plen)) { place_dep(pi, r, c, plen); pr = true; }
                if (pr) return true;
                remove_dep(ui); remove_dep(cur);
            }
            else { remove_dep(ui); }
            place_dep(pi, orow, ocol, plen);
        }
    }
    return false;
}

bool swap_placed(int cur, std::vector<int>& placed) {
    if (placed.empty()) return false;
    std::shuffle(placed.begin(), placed.end(), rng);
    for (int pi : placed) {
        int orow = get_row(deps[pi]);
        int ocol = (std::max)(0, static_cast<int>(deps[pi].center.x - deps[pi].length * 0.5));
        int plen = get_department_length(pi);
        int clen = get_department_length(cur);
        if (clen > plen) continue;
        remove_dep(pi);
        if (!can_place(orow, ocol, clen)) { place_dep(pi, orow, ocol, plen); continue; }
        place_dep(cur, orow, ocol, clen);
        bool pr = false;
        for (int r = 0; r < grid_rows && !pr; ++r)
            for (int c = 0; c <= grid_cols - plen && !pr; ++c)
                if (can_place(r, c, plen)) { place_dep(pi, r, c, plen); pr = true; }
        if (pr) return true;
        remove_dep(cur); place_dep(pi, orow, ocol, plen);
    }
    return false;
}

bool rearrange(int cur, const std::vector<int>& placed_order) {
    std::vector<int> placed, unplaced;
    for (int i = 0; i < dep_count; ++i) {
        if (i == cur) continue;
        if (deps[i].placed) placed.push_back(i);
        else unplaced.push_back(i);
    }

    if (!placed_order.empty()) {
        int last_placed = placed_order.back();
        if (move_last_to_history_position(last_placed, cur)) return true;
    }

    int s = rand() % 3;
    if (s == 0 && (move_to_span(cur, placed) || random_replace(cur, placed, unplaced) || swap_placed(cur, placed))) return true;
    if (s == 1 && (random_replace(cur, placed, unplaced) || move_to_span(cur, placed) || swap_placed(cur, placed))) return true;
    if (s == 2 && (swap_placed(cur, placed) || random_replace(cur, placed, unplaced) || move_to_span(cur, placed))) return true;
    return false;
}

// ==================== ФУНКЦИИ ДЛЯ ПРОВЕРКИ ГРАНИЦ ====================

// Проверка, касается ли подразделение левой стены
bool touches_left_wall(int dept_idx) {
    if (!deps[dept_idx].placed) return false;
    double left = deps[dept_idx].center.x - deps[dept_idx].length * 0.5;
    return std::abs(left) < EPSILON;
}

// Проверка, касается ли подразделение правой стены
bool touches_right_wall(int dept_idx) {
    if (!deps[dept_idx].placed) return false;
    double right = deps[dept_idx].center.x + deps[dept_idx].length * 0.5;
    return std::abs(right - grid_cols) < EPSILON;
}

// Проверка, есть ли у подразделения общая левая граница с соседом
bool has_left_border_with_neighbor(int dept_idx) {
    if (!deps[dept_idx].placed) return false;

    int row = get_row(deps[dept_idx]);
    double dept_left = deps[dept_idx].center.x - deps[dept_idx].length * 0.5;

    for (int i = 0; i < dep_count; ++i) {
        if (i == dept_idx || !deps[i].placed) continue;
        if (get_row(deps[i]) != row) continue;

        double neighbor_right = deps[i].center.x + deps[i].length * 0.5;

        if (std::abs(dept_left - neighbor_right) < EPSILON) {
            return true;
        }
    }
    return false;
}

// Проверка, есть ли у подразделения общая правая граница с соседом
bool has_right_border_with_neighbor(int dept_idx) {
    if (!deps[dept_idx].placed) return false;

    int row = get_row(deps[dept_idx]);
    double dept_right = deps[dept_idx].center.x + deps[dept_idx].length * 0.5;

    for (int i = 0; i < dep_count; ++i) {
        if (i == dept_idx || !deps[i].placed) continue;
        if (get_row(deps[i]) != row) continue;

        double neighbor_left = deps[i].center.x - deps[i].length * 0.5;

        if (std::abs(dept_right - neighbor_left) < EPSILON) {
            return true;
        }
    }
    return false;
}

// ==================== ФУНКЦИИ ДЛЯ ПЕРЕМЕЩЕНИЯ ПОДРАЗДЕЛЕНИЙ ====================

// Получить индекс подразделения по ID
int get_index_by_id(int id) {
    for (int i = 0; i < dep_count; ++i) {
        if (deps[i].id == id) return i;
    }
    return -1;
}

// Функция обмена двух любых подразделений
bool swap_two() {
    std::cout << "\n=== ОБМЕН ДВУХ ПОДРАЗДЕЛЕНИЙ ===\n";
    show_placed();

    int id1 = read_int("ID первого: ", 1, dep_count);
    int id2 = read_int("ID второго: ", 1, dep_count);
    if (id1 == id2) {
        std::cout << "Нельзя обменять подразделение само с собой.\n";
        return false;
    }

    int i1 = get_index_by_id(id1);
    int i2 = get_index_by_id(id2);

    if (i1 == -1 || i2 == -1 || !deps[i1].placed || !deps[i2].placed) {
        std::cout << "Одно или оба подразделения не найдены или не размещены.\n";
        return false;
    }

    int row1 = get_row(deps[i1]);
    int row2 = get_row(deps[i2]);
    int len1 = get_department_length(i1);
    int len2 = get_department_length(i2);
    int col1 = static_cast<int>(deps[i1].center.x - deps[i1].length * 0.5);
    int col2 = static_cast<int>(deps[i2].center.x - deps[i2].length * 0.5);

    std::cout << "\nМеняем местами:\n";
    std::cout << "  " << deps[i1].name << " (пролёт " << row1 << ", x=" << col1 << ", длина " << len1 << ")\n";
    std::cout << "  " << deps[i2].name << " (пролёт " << row2 << ", x=" << col2 << ", длина " << len2 << ")\n";

    // Удаляем оба подразделения
    remove_dep(i1);
    remove_dep(i2);

    // Пробуем разместить их на местах друг друга
    bool success = false;

    if (row1 == row2) {
        // В одном пролёте
        if (can_place(row1, col2, len1) && can_place(row1, col1, len2)) {
            place_dep(i1, row1, col2, len1);
            place_dep(i2, row1, col1, len2);
            success = true;
            std::cout << "Подразделения обменяны позициями в пролёте " << row1 << "\n";
        }
        else {
            // Не получилось обменять позициями - ищем другие места
            int new_col1 = -1, new_col2 = -1;
            for (int c = 0; c <= grid_cols - len1; ++c) {
                if (can_place(row1, c, len1)) { new_col1 = c; break; }
            }
            for (int c = 0; c <= grid_cols - len2; ++c) {
                if (can_place(row1, c, len2)) { new_col2 = c; break; }
            }
            if (new_col1 != -1 && new_col2 != -1) {
                place_dep(i1, row1, new_col1, len1);
                place_dep(i2, row1, new_col2, len2);
                success = true;
                std::cout << "Подразделения размещены на свободных местах в пролёте " << row1 << "\n";
            }
        }
    }
    else {
        // В разных пролётах
        if (can_place(row2, col1, len1) && can_place(row1, col2, len2)) {
            place_dep(i1, row2, col1, len1);
            place_dep(i2, row1, col2, len2);
            success = true;
            std::cout << "Подразделения обменяны пролётами\n";
        }
        else {
            // Не получилось - ищем другие места
            int new_col1 = -1, new_col2 = -1;
            for (int c = 0; c <= grid_cols - len1; ++c) {
                if (can_place(row2, c, len1)) { new_col1 = c; break; }
            }
            for (int c = 0; c <= grid_cols - len2; ++c) {
                if (can_place(row1, c, len2)) { new_col2 = c; break; }
            }
            if (new_col1 != -1 && new_col2 != -1) {
                place_dep(i1, row2, new_col1, len1);
                place_dep(i2, row1, new_col2, len2);
                success = true;
                std::cout << "Подразделения размещены на свободных местах\n";
            }
        }
    }

    if (!success) {
        std::cout << "Не удалось обменять подразделения. Восстановление...\n";
        place_dep(i1, row1, col1, len1);
        place_dep(i2, row2, col2, len2);
        return false;
    }

    // Переупорядочиваем пролёты (сдвигаем к левому краю)
    if (row1 == row2) {
        std::vector<std::pair<int, int>> depts_in_row;
        for (int i = 0; i < dep_count; ++i) {
            if (deps[i].placed && get_row(deps[i]) == row1) {
                int left = static_cast<int>(deps[i].center.x - deps[i].length * 0.5);
                depts_in_row.emplace_back(left, i);
            }
        }
        std::sort(depts_in_row.begin(), depts_in_row.end());

        int current_x = 0;
        for (auto& [left, idx] : depts_in_row) {
            int len = get_department_length(idx);
            if (left != current_x && can_place(row1, current_x, len)) {
                remove_dep(idx);
                place_dep(idx, row1, current_x, len);
            }
            current_x += len;
        }
    }
    else {
        for (int r : {row1, row2}) {
            std::vector<std::pair<int, int>> depts_in_row;
            for (int i = 0; i < dep_count; ++i) {
                if (deps[i].placed && get_row(deps[i]) == r) {
                    int left = static_cast<int>(deps[i].center.x - deps[i].length * 0.5);
                    depts_in_row.emplace_back(left, i);
                }
            }
            std::sort(depts_in_row.begin(), depts_in_row.end());

            int current_x = 0;
            for (auto& [left, idx] : depts_in_row) {
                int len = get_department_length(idx);
                if (left != current_x && can_place(r, current_x, len)) {
                    remove_dep(idx);
                    place_dep(idx, r, current_x, len);
                }
                current_x += len;
            }
        }
    }

    std::cout << "Обмен успешно завершён.\n";
    return true;
}

// Универсальная функция перемещения подразделения (оптимизированная)
bool move_department() {
    std::cout << "\n=== ПЕРЕМЕЩЕНИЕ ПОДРАЗДЕЛЕНИЯ ===\n";
    std::cout << "1. Переместить размещённое подразделение\n";
    std::cout << "2. Разместить неразмещённое подразделение\n";
    int mode = read_menu_choice("Выбор: ", 1, 2);

    int idx = -1;
    if (mode == 1) {
        show_placed();
        int id = read_int("ID размещённого подразделения: ", 1, dep_count);
        idx = get_index_by_id(id);
        if (idx == -1 || !deps[idx].placed) {
            std::cout << "Подразделение не найдено или не размещено.\n";
            return false;
        }
    }
    else {
        show_unplaced();
        int id = read_int("ID неразмещённого подразделения: ", 1, dep_count);
        idx = get_index_by_id(id);
        if (idx == -1 || deps[idx].placed) {
            std::cout << "Подразделение не найдено или уже размещено.\n";
            return false;
        }
    }

    int len = get_department_length(idx);
    int old_row = -1, old_col = -1;
    bool was_placed = deps[idx].placed;

    if (was_placed) {
        old_row = get_row(deps[idx]);
        old_col = static_cast<int>(deps[idx].center.x - deps[idx].length * 0.5);
        std::cout << "\nПеремещаем " << deps[idx].name << ":\n";
        std::cout << "  Текущая позиция: пролёт " << old_row << ", x=" << old_col << "-" << old_col + len << "\n";

        remove_dep(idx);
        std::cout << "  Подразделение удалено из компоновки.\n";
        update_grid_from_departments(); // Обновляем сетку после удаления
    }
    else {
        std::cout << "\nРазмещаем " << deps[idx].name << " (длина " << len << " м):\n";
    }

    // Поиск возможных мест для размещения
    std::vector<std::tuple<int, int, std::string>> options;

    for (int r = 0; r < grid_rows; ++r) {
        // Привязка к левой стене
        if (can_place(r, 0, len)) {
            options.emplace_back(r, 0, "пролёт " + std::to_string(r) + ", прилегание к ЛЕВОЙ стене");
        }

        // Привязка к правой стене
        if (can_place(r, grid_cols - len, len)) {
            options.emplace_back(r, grid_cols - len, "пролёт " + std::to_string(r) + ", прилегание к ПРАВОЙ стене");
        }

        // Привязка к соседям
        for (int i = 0; i < dep_count; ++i) {
            if (i == idx || !deps[i].placed) continue;
            if (get_row(deps[i]) != r) continue;

            int neighbor_left = static_cast<int>(deps[i].center.x - deps[i].length * 0.5);
            int neighbor_right = neighbor_left + static_cast<int>(deps[i].length);

            // Слева от соседа (подразделение будет справа от соседа)
            int pos_left = neighbor_left - len;
            if (pos_left >= 0 && can_place(r, pos_left, len)) {
                if (std::abs(pos_left + len - neighbor_left) < EPSILON) {
                    options.emplace_back(r, pos_left, "пролёт " + std::to_string(r) + ", прилегание СПРАВА к " + deps[i].name);
                }
            }

            // Справа от соседа (подразделение будет слева от соседа)
            int pos_right = neighbor_right;
            if (pos_right + len <= grid_cols && can_place(r, pos_right, len)) {
                if (std::abs(pos_right - neighbor_right) < EPSILON) {
                    options.emplace_back(r, pos_right, "пролёт " + std::to_string(r) + ", прилегание СЛЕВА к " + deps[i].name);
                }
            }
        }
    }

    // Удаляем дубликаты
    std::sort(options.begin(), options.end());
    options.erase(std::unique(options.begin(), options.end()), options.end());

    if (options.empty()) {
        std::cout << "\n  Нет доступных мест для размещения!\n";
        if (was_placed) {
            std::cout << "  Восстановление на прежнюю позицию...\n";
            place_dep(idx, old_row, old_col, len);
            update_grid_from_departments();
        }
        return false;
    }

    std::cout << "\n  Доступные варианты размещения:\n";
    for (size_t i = 0; i < options.size(); ++i) {
        std::cout << "    " << i + 1 << ": " << std::get<2>(options[i])
            << ", x=" << std::get<1>(options[i]) << "-" << std::get<1>(options[i]) + len << "\n";
        if (i >= 29) {
            std::cout << "    ... (показаны первые 30)\n";
            break;
        }
    }

    int choice = read_int("  Выберите вариант (0-отмена): ", 0, static_cast<int>(options.size()));
    if (choice == 0) {
        std::cout << "  Отмена.\n";
        if (was_placed) {
            std::cout << "  Восстановление на прежнюю позицию...\n";
            place_dep(idx, old_row, old_col, len);
            update_grid_from_departments();
        }
        return false;
    }

    auto& opt = options[choice - 1];
    int new_row = std::get<0>(opt);
    int new_col = std::get<1>(opt);

    place_dep(idx, new_row, new_col, len);
    update_grid_from_departments(); // Обновляем сетку после размещения
    std::cout << "\n  Подразделение размещено: пролёт " << new_row << ", x=" << new_col << "-" << new_col + len << "\n";

    // Переупорядочиваем пролёты (сдвигаем к левому краю)
    if (was_placed && old_row != -1 && old_row != new_row) {
        // Переупорядочиваем старый пролёт
        std::vector<std::pair<int, int>> old_depts;
        for (int i = 0; i < dep_count; ++i) {
            if (deps[i].placed && get_row(deps[i]) == old_row) {
                int left = static_cast<int>(deps[i].center.x - deps[i].length * 0.5);
                old_depts.emplace_back(left, i);
            }
        }
        std::sort(old_depts.begin(), old_depts.end());
        int current_x = 0;
        for (auto& [left, i] : old_depts) {
            int l = get_department_length(i);
            if (left != current_x && can_place(old_row, current_x, l)) {
                remove_dep(i);
                place_dep(i, old_row, current_x, l);
            }
            current_x += l;
        }
    }

    // Переупорядочиваем новый пролёт
    std::vector<std::pair<int, int>> new_depts;
    for (int i = 0; i < dep_count; ++i) {
        if (deps[i].placed && get_row(deps[i]) == new_row) {
            int left = static_cast<int>(deps[i].center.x - deps[i].length * 0.5);
            new_depts.emplace_back(left, i);
        }
    }
    std::sort(new_depts.begin(), new_depts.end());
    int current_x = 0;
    for (auto& [left, i] : new_depts) {
        int l = get_department_length(i);
        if (left != current_x && can_place(new_row, current_x, l)) {
            remove_dep(i);
            place_dep(i, new_row, current_x, l);
        }
        current_x += l;
    }

    update_grid_from_departments();
    fix_overlaps();

    std::cout << "\nПеремещение успешно завершено.\n";
    return true;
}

// Функция удаления подразделения (без сдвига других)
bool remove_placed_department() {
    std::cout << "\n=== УДАЛЕНИЕ ПОДРАЗДЕЛЕНИЯ ===\n";
    show_placed();

    int id = read_int("ID подразделения для удаления: ", 1, dep_count);
    int idx = get_index_by_id(id);

    if (idx == -1 || !deps[idx].placed) {
        std::cout << "Подразделение не найдено или не размещено.\n";
        return false;
    }

    int row = get_row(deps[idx]);
    int col = static_cast<int>(deps[idx].center.x - deps[idx].length * 0.5);
    int len = get_department_length(idx);

    std::cout << "Удаляем " << deps[idx].name << " из пролёта " << row
        << " (позиция " << col << " - " << col + len << ")\n";

    remove_dep(idx);
    update_grid_from_departments();
    std::cout << deps[idx].name << " удалён.\n";
    std::cout << "На месте удалённого подразделения образовалось пустое место.\n";

    fix_overlaps();

    return true;
}

// ==================== АЛГОРИТМ CORELAP ПО БЛОК-СХЕМЕ ====================

// Поиск участка с максимальной суммарной оценкой среди неразмещённых
int find_max_score_unplaced(const std::vector<bool>& is_placed) {
    int max_idx = -1;
    int max_score = -1;
    for (int i = 0; i < dep_count; ++i) {
        if (!is_placed[i] && deps[i].total_score > max_score) {
            max_score = deps[i].total_score;
            max_idx = i;
        }
    }
    return max_idx;
}

// Проверка, есть ли несколько участков с одинаковой максимальной оценкой
bool has_multiple_with_max_score(const std::vector<bool>& is_placed, int max_score) {
    int count = 0;
    for (int i = 0; i < dep_count; ++i) {
        if (!is_placed[i] && deps[i].total_score == max_score) {
            count++;
        }
    }
    return count > 1;
}

// Выбор участка с максимальной площадью среди нескольких с одинаковой оценкой
int select_max_area_among_score(const std::vector<bool>& is_placed, int target_score) {
    int max_idx = -1;
    double max_area = -1;
    for (int i = 0; i < dep_count; ++i) {
        if (!is_placed[i] && deps[i].total_score == target_score && deps[i].area > max_area) {
            max_area = deps[i].area;
            max_idx = i;
        }
    }
    return max_idx;
}

// Поиск неразмещённого участка с оценкой V и более с уже размещённым
int find_unplaced_with_score(int placed_idx, int min_score) {
    int best_idx = -1;
    int best_score = -1;
    for (int i = 0; i < dep_count; ++i) {
        if (deps[i].placed) continue;
        if (score_matrix[placed_idx][i] >= min_score && score_matrix[placed_idx][i] > best_score) {
            best_score = score_matrix[placed_idx][i];
            best_idx = i;
        }
    }
    return best_idx;
}

// Основной алгоритм размещения по блок-схеме
void corelap_placement(const Building& b) {
    // Инициализация
    for (int i = 0; i < dep_count; ++i) {
        deps[i].placed = false;
        deps[i].position_history.clear();
    }
    for (int i = 0; i < grid_rows * grid_cols; ++i) grid[i] = 0;
    if (!dep_count) return;

    std::vector<bool> is_placed(dep_count, false);
    std::vector<int> placed_order;

    // --- ШАГ 1: Выбор первого участка ---
    int first_idx = find_max_score_unplaced(is_placed);
    if (first_idx == -1) return;

    int max_score_first = deps[first_idx].total_score;

    if (has_multiple_with_max_score(is_placed, max_score_first)) {
        first_idx = select_max_area_among_score(is_placed, max_score_first);
        std::cout << "Несколько участков с max оценкой " << max_score_first
            << ". Выбран участок с max площадью: " << deps[first_idx].name << "\n";
    }

    int clen = get_department_length(first_idx);
    int crow = grid_rows / 2;
    int ccol = clamp_int((grid_cols / 2) - (clen / 2), 0, grid_cols - clen);
    place_dep(first_idx, crow, ccol, clen);
    is_placed[first_idx] = true;
    placed_order.push_back(first_idx);

    std::cout << "\nПервый участок (оценка " << deps[first_idx].total_score
        << ", площадь " << deps[first_idx].area << "): "
        << deps[first_idx].name << " [" << deps[first_idx].id << "]\n";

    // --- ШАГ 2: Последовательное размещение ---
    int V = 100;

    for (int step = 1; step < dep_count; ++step) {
        int next_idx = -1;

        while (next_idx == -1 && V > 0) {
            for (int placed_idx : placed_order) {
                int candidate = find_unplaced_with_score(placed_idx, V);
                if (candidate != -1) {
                    next_idx = candidate;
                    break;
                }
            }

            if (next_idx == -1) {
                V--;
                std::cout << "  Снижение порога V = " << V << "\n";
            }
        }

        if (next_idx == -1) {
            for (int i = 0; i < dep_count; ++i) {
                if (!is_placed[i]) {
                    next_idx = i;
                    break;
                }
            }
        }

        if (next_idx == -1) break;

        int dlen = get_department_length(next_idx);
        bool ok = false;

        std::vector<std::pair<int, int>> candidate_positions;

        for (int pid : placed_order) {
            int row = get_row(deps[pid]);
            int left = static_cast<int>(deps[pid].center.x - deps[pid].length * 0.5);
            int right = left + static_cast<int>(deps[pid].length);

            if (left - dlen >= 0) candidate_positions.emplace_back(row, left - dlen);
            if (right + dlen <= grid_cols) candidate_positions.emplace_back(row, right);
        }

        for (int pid : placed_order) {
            int row = get_row(deps[pid]);
            int left = static_cast<int>(deps[pid].center.x - deps[pid].length * 0.5);
            if (row > 0) candidate_positions.emplace_back(row - 1, left);
            if (row + 1 < grid_rows) candidate_positions.emplace_back(row + 1, left);
        }

        for (auto [r, c] : candidate_positions) {
            if (can_place(r, c, dlen)) {
                place_dep(next_idx, r, c, dlen);
                ok = true;
                break;
            }
        }

        if (!ok) {
            for (int r = 0; r < grid_rows && !ok; ++r) {
                for (int c = 0; c <= grid_cols - dlen && !ok; ++c) {
                    if (can_place(r, c, dlen)) {
                        place_dep(next_idx, r, c, dlen);
                        ok = true;
                    }
                }
            }
        }

        if (!ok) {
            for (int r = 0; r < grid_rows && !ok; ++r) {
                for (int c = 0; c <= grid_cols - dlen && !ok; ++c) {
                    int cn = 0;
                    for (int k = 0; k < dlen; ++k) if (get_grid(r, c + k)) ++cn;
                    if (cn > 0 && cn < dlen && shift_row(r, c, dlen) && can_place(r, c, dlen)) {
                        place_dep(next_idx, r, c, dlen);
                        ok = true;
                    }
                }
            }
        }

        if (!ok && rearrange(next_idx, placed_order)) {
            ok = true;
        }

        if (ok) {
            is_placed[next_idx] = true;
            placed_order.push_back(next_idx);
            std::cout << "  + " << deps[next_idx].name << " [" << deps[next_idx].id
                << "] (порог V=" << V << ")\n";
        }
        else {
            std::cout << "  - " << deps[next_idx].name << " НЕ РАЗМЕЩЕНО\n";
        }

        if (V > 1) V = 100;
    }

    // Размещение оставшихся
    for (int i = 0; i < dep_count; ++i) {
        if (!is_placed[i]) {
            int dlen = get_department_length(i);
            for (int r = 0; r < grid_rows && !deps[i].placed; ++r) {
                for (int c = 0; c <= grid_cols - dlen && !deps[i].placed; ++c) {
                    if (can_place(r, c, dlen)) {
                        place_dep(i, r, c, dlen);
                        std::cout << "  + " << deps[i].name << " (дополнительно)\n";
                    }
                }
            }
        }
    }

    // Переупорядочиваем все пролёты
    for (int r = 0; r < grid_rows; ++r) {
        std::vector<std::pair<int, int>> depts_in_row;
        for (int i = 0; i < dep_count; ++i) {
            if (deps[i].placed && get_row(deps[i]) == r) {
                int left = static_cast<int>(deps[i].center.x - deps[i].length * 0.5);
                depts_in_row.emplace_back(left, i);
            }
        }
        std::sort(depts_in_row.begin(), depts_in_row.end());

        int current_x = 0;
        for (auto& [left, idx] : depts_in_row) {
            int len = get_department_length(idx);
            if (left != current_x && can_place(r, current_x, len)) {
                remove_dep(idx);
                place_dep(idx, r, current_x, len);
            }
            current_x += len;
        }
    }

    fix_overlaps();

    int ov = 0, cn = 0;
    for (int i = 0; i < dep_count; ++i) {
        for (int j = i + 1; j < dep_count; ++j) {
            if (!deps[i].placed || !deps[j].placed || get_row(deps[i]) != get_row(deps[j])) continue;
            double il = deps[i].center.x - deps[i].length * 0.5, ir = deps[i].center.x + deps[i].length * 0.5;
            double jl = deps[j].center.x - deps[j].length * 0.5, jr = deps[j].center.x + deps[j].length * 0.5;
            if ((il > jl && ir < jr) || (jl > il && jr < ir)) ++cn;
            else if (il < jr && ir > jl && ir != jl && jr != il) ++ov;
        }
    }
    std::cout << "\nВложений: " << cn << ", наложений: " << ov << "\n";
    if (!cn && !ov) std::cout << "Размещение корректно.\n";

    if (read_int("\nРучные перестановки? (1-да, 0-нет): ", 0, 1)) interactive_rearrange(b);
}

// ==================== ИНТЕРАКТИВНЫЕ ПЕРЕСТАНОВКИ ====================

void show_placed() {
    for (int i = 0; i < dep_count; ++i)
        if (deps[i].placed)
            std::cout << "  " << deps[i].name << " [ID:" << deps[i].id << "] пролёт " << get_row(deps[i])
            << ", x=" << std::fixed << std::setprecision(1) << deps[i].center.x
            << ", " << deps[i].width << "x" << deps[i].length << " м\n";
}

void show_unplaced() {
    bool any = false;
    for (int i = 0; i < dep_count; ++i)
        if (!deps[i].placed) {
            std::cout << "  " << deps[i].name << " [ID:" << deps[i].id << "] длина " << deps[i].length << " м\n";
            any = true;
        }
    if (!any) std::cout << "  (все размещены)\n";
}

void interactive_rearrange(const Building& b) {
    while (true) {
        std::cout << "\n========================================\n        ИНТЕРАКТИВНЫЕ ПЕРЕСТАНОВКИ\n========================================\n";
        std::cout << "1. Обменять два подразделения\n";
        std::cout << "2. Переместить подразделение\n";
        std::cout << "3. Удалить подразделение\n";
        std::cout << "4. Показать компоновку\n";
        std::cout << "5. Показать метрику\n";
        std::cout << "6. Сохранить HTML\n";
        std::cout << "7. Выход\n";

        int c = read_menu_choice("Выбор: ", 1, 7);
        bool ch = false;
        switch (c) {
        case 1: ch = swap_two(); break;
        case 2: ch = move_department(); break;
        case 3: ch = remove_placed_department(); break;
        case 4: std::cout << "\nКомпоновка:\n"; show_placed(); break;
        case 5: {
            std::cout << "\nГрузопоток: " << std::fixed << std::setprecision(2) << calc_total_flow() << " т*м/год\nСвязи:\n";
            for (int i = 0; i < dep_count; ++i)
                for (int j = i + 1; j < dep_count; ++j) {
                    double pf = flow_original[i][j] + flow_original[j][i];
                    if (pf > 0 && deps[i].placed && deps[j].placed)
                        std::cout << "  " << deps[i].name << " <-> " << deps[j].name << ": " << pf << " т/г, " << manhattan(deps[i].center, deps[j].center) << " м\n";
                }
            break;
        }
        case 6: draw_html(b); break;
        case 7: return;
        }
        if (ch) {
            fix_overlaps();
            double tf = calc_total_flow();
            int pl = 0; for (int i = 0; i < dep_count; ++i) if (deps[i].placed) ++pl;
            std::cout << "Грузопоток: " << tf << " т*м/год | Размещено: " << pl << "/" << dep_count << "\n";
        }
    }
}

// ==================== РАСЧЁТЫ ====================

void calc_flows_and_scores() {
    min_flow = 1e9; max_flow = 0;
    for (int i = 0; i < dep_count; ++i) {
        deps[i].total_flow = 0;
        for (int j = 0; j < dep_count; ++j) deps[i].total_flow += flow_sum[i][j];
        deps[i].total_flow *= 0.5;
        if (deps[i].total_flow < min_flow) min_flow = deps[i].total_flow;
        if (deps[i].total_flow > max_flow) max_flow = deps[i].total_flow;
    }
}

void assign_cat() {
    if (max_flow - min_flow < EPSILON) {
        for (int i = 0; i < dep_count; ++i) deps[i].category = MEDIUM;
        cat_step = 1.0;
    }
    else {
        cat_step = (max_flow - min_flow) / MAX_CATEGORIES;
        for (int i = 0; i < dep_count; ++i) {
            double f = deps[i].total_flow;
            if (f >= max_flow - cat_step) deps[i].category = VERY_IMPORTANT;
            else if (f >= max_flow - 2 * cat_step) deps[i].category = IMPORTANT;
            else if (f >= max_flow - 3 * cat_step) deps[i].category = MEDIUM;
            else if (f >= max_flow - 4 * cat_step) deps[i].category = UNIMPORTANT;
            else deps[i].category = VERY_UNIMPORTANT;
        }
    }
}

void calc_scores() {
    for (int i = 0; i < dep_count; ++i) {
        for (int j = 0; j < dep_count; ++j) {
            score_matrix[i][j] = 0;
            if (i == j) continue;
            double pf = flow_sum[i][j] * 0.5;
            if (pf < EPSILON) continue;
            if (pf >= max_flow - cat_step) score_matrix[i][j] = CATEGORY_SCORES[VERY_IMPORTANT];
            else if (pf >= max_flow - 2 * cat_step) score_matrix[i][j] = CATEGORY_SCORES[IMPORTANT];
            else if (pf >= max_flow - 3 * cat_step) score_matrix[i][j] = CATEGORY_SCORES[MEDIUM];
            else if (pf >= max_flow - 4 * cat_step) score_matrix[i][j] = CATEGORY_SCORES[UNIMPORTANT];
        }
    }
    for (int i = 0; i < dep_count; ++i) {
        deps[i].total_score = 0;
        for (int j = 0; j < dep_count; ++j) deps[i].total_score += score_matrix[i][j];
    }
}

void calc_dimensions(const Building& b) {
    for (int i = 0; i < dep_count; ++i) {
        deps[i].width = b.span_width;
        deps[i].length = (std::max)(1.0, deps[i].area / deps[i].width);
        if (deps[i].length > b.max_length) deps[i].length = b.max_length;
    }
}

double calc_total_flow() {
    double total = 0;
    for (int i = 0; i < dep_count; ++i)
        for (int j = i + 1; j < dep_count; ++j) {
            double pf = flow_original[i][j] + flow_original[j][i];
            if (pf > 0 && deps[i].placed && deps[j].placed)
                total += pf * manhattan(deps[i].center, deps[j].center);
        }
    return total;
}

// ==================== ВВОД ДАННЫХ ====================

Building input_building() {
    Building b;
    std::cout << "\n=== ПАРАМЕТРЫ ЗДАНИЯ ===\n";
    b.span_width = read_double("Ширина пролёта (м): ", 1.0, 100.0);
    b.span_count = read_int("Количество пролётов: ", 1, 20);
    b.max_length = read_double("Длина здания (м): ", 1.0, 500.0);
    return b;
}

void input_departments() {
    dep_count = read_int("\nКоличество подразделений: ", 1, MAX_DEPS);
    std::cout << "\nВводите подразделения в формате: Название, Площадь\nПример: Цех1, 500\n\n";
    clear_input();
    for (int i = 0; i < dep_count; ++i) {
        deps[i].id = i + 1; deps[i].placed = false; deps[i].total_flow = 0; deps[i].total_score = 0; deps[i].category = VERY_UNIMPORTANT;
        while (true) {
            std::cout << "Подразделение #" << i + 1 << ": ";
            std::string line; std::getline(std::cin, line);
            size_t comma = line.find(',');
            if (comma == std::string::npos) { std::cout << "Ошибка! Нужна запятая.\n"; continue; }
            deps[i].name = line.substr(0, comma);
            while (!deps[i].name.empty() && deps[i].name.front() == ' ') deps[i].name.erase(0, 1);
            while (!deps[i].name.empty() && deps[i].name.back() == ' ') deps[i].name.pop_back();
            std::string as = line.substr(comma + 1);
            while (!as.empty() && as.front() == ' ') as.erase(0, 1);
            double area = strtod(as.c_str(), nullptr);
            if (area <= 0 || area > 10000) { std::cout << "Ошибка! Площадь 1-10000.\n"; continue; }
            deps[i].area = area; break;
        }
    }
}

void input_flows() {
    for (int i = 0; i < dep_count; ++i)
        for (int j = 0; j < dep_count; ++j) flow_sum[i][j] = flow_original[i][j] = 0;
    std::cout << "\n=== МАТРИЦА ГРУЗОПОТОКОВ ===\nВводите значения построчно (0 или '-' = нет потока)\nГрузопотоки встречных направлений АВТОМАТИЧЕСКИ СУММИРУЮТСЯ\n\n";
    double temp[MAX_DEPS][MAX_DEPS] = {};
    for (int i = 0; i < dep_count; ++i) {
        std::cout << "Строка ID" << i + 1 << ": ";
        for (int j = 0; j < dep_count; ++j) {
            if (i == j) continue;
            std::string in; std::cin >> in;
            if (in != "0" && in != "-" && in != "0.0") { double v = strtod(in.c_str(), nullptr); if (v > 0) temp[i][j] = v; }
        }
    }
    for (int i = 0; i < dep_count; ++i)
        for (int j = 0; j < dep_count; ++j)
            if (i != j) { flow_original[i][j] = temp[i][j]; double s = temp[i][j] + temp[j][i]; flow_sum[i][j] = flow_sum[j][i] = s; }
}

// ==================== СОХРАНЕНИЕ/ВОССТАНОВЛЕНИЕ ====================

void save_data(const Building& b) {
    saved_dep_count = dep_count;
    for (int i = 0; i < dep_count; ++i) { saved_deps[i] = deps[i]; saved_deps[i].placed = false; saved_deps[i].center = { 0,0 }; saved_deps[i].width = saved_deps[i].length = 0; saved_deps[i].total_flow = 0; saved_deps[i].total_score = 0; saved_deps[i].category = VERY_UNIMPORTANT; }
    for (int i = 0; i < dep_count; ++i) for (int j = 0; j < dep_count; ++j) { saved_flow_sum[i][j] = flow_sum[i][j]; saved_score_matrix[i][j] = score_matrix[i][j]; }
    saved_building = b; has_saved_data = true;
}

void restore_data() {
    dep_count = saved_dep_count;
    for (int i = 0; i < dep_count; ++i) deps[i] = saved_deps[i];
    for (int i = 0; i < dep_count; ++i) for (int j = 0; j < dep_count; ++j) { flow_sum[i][j] = saved_flow_sum[i][j]; flow_original[i][j] = saved_flow_sum[i][j] * 0.5; score_matrix[i][j] = saved_score_matrix[i][j]; }
}

// ==================== ВЫВОД ====================

void print_categories() {
    std::cout << "\n=== РАНЖИРОВАНИЕ ===\nДиапазон: " << std::fixed << std::setprecision(1) << min_flow << " - " << max_flow << " т/г\n";
    std::cout << "Баллы: очень важный=" << CATEGORY_SCORES[0] << ", важный=" << CATEGORY_SCORES[1] << ", средний=" << CATEGORY_SCORES[2] << ", неважный=" << CATEGORY_SCORES[3] << "\n\n";
    int cnt[MAX_CATEGORIES] = {};
    for (int i = 0; i < dep_count; ++i) ++cnt[deps[i].category];
    for (int c = 0; c < MAX_CATEGORIES; ++c) {
        if (!cnt[c]) continue;
        std::cout << "[" << CATEGORY_NAMES[c] << " (" << CATEGORY_SCORES[c] << " баллов)] " << cnt[c] << ":\n";
        for (int i = 0; i < dep_count; ++i) if (deps[i].category == c) std::cout << "  " << deps[i].name << " [" << deps[i].id << "] поток " << deps[i].total_flow << " т/г, оценка " << deps[i].total_score << "\n";
        std::cout << "\n";
    }
}

void print_results(long long ms) {
    double tf = calc_total_flow();
    int pl = 0; for (int i = 0; i < dep_count; ++i) if (deps[i].placed) ++pl;
    std::cout << "\n========================================\n     РЕЗУЛЬТАТЫ #" << run_number << "\n========================================\n";
    std::cout << "\n--- Детализация ---\n" << std::left << std::setw(25) << "Связь" << std::setw(12) << "Поток" << std::setw(15) << "Расст" << std::setw(18) << "Вклад\n" << std::string(70, '-') << "\n";
    double ct = 0;
    for (int i = 0; i < dep_count; ++i)
        for (int j = i + 1; j < dep_count; ++j) {
            double pf = flow_original[i][j] + flow_original[j][i];
            if (pf > 0 && deps[i].placed && deps[j].placed) {
                double d = manhattan(deps[i].center, deps[j].center), c = pf * d; ct += c;
                std::cout << std::left << std::setw(25) << (deps[i].name + " <-> " + deps[j].name) << std::setw(12) << std::setprecision(1) << pf << std::setw(15) << d << std::setw(18) << std::setprecision(2) << c << "\n";
            }
        }
    std::cout << std::string(70, '-') << "\n" << std::setw(52) << "Суммарный грузопоток:" << std::setprecision(2) << ct << " т*м/год\n";
    std::cout << "\nРазмещено: " << pl << "/" << dep_count << " | Время: " << ms << " мс\n\n";
    std::cout << std::left << std::setw(4) << "ID" << std::setw(20) << "Название" << std::setw(8) << "Поток" << std::setw(8) << "Оценка" << std::setw(18) << "Размер (шxд)" << std::setw(18) << "Категория" << "Позиция\n" << std::string(100, '-') << "\n";
    for (int i = 0; i < dep_count; ++i) {
        std::cout << std::left << std::setw(4) << deps[i].id << std::setw(20) << deps[i].name << std::setw(8) << std::setprecision(1) << deps[i].total_flow << std::setw(8) << deps[i].total_score;
        char bf[20]; sprintf_s(bf, sizeof(bf), "%.1f x %.1f", deps[i].width, deps[i].length);
        std::cout << std::setw(18) << bf << std::setw(18) << CATEGORY_NAMES[deps[i].category];
        if (deps[i].placed) std::cout << "(" << std::setprecision(1) << deps[i].center.x << ", " << deps[i].center.y << ")\n";
        else std::cout << "НЕ РАЗМЕЩЕНО\n";
    }
}

void draw_html(const Building& b) {
    static int html_counter = 0;
    html_counter++;
    char fn[100];
    sprintf_s(fn, sizeof(fn), "layout_%d_%d.html", run_number, html_counter);

    std::ofstream out(fn);
    if (!out) return;

    int W = static_cast<int>(b.max_length * HTML_SCALE), H = static_cast<int>(b.span_count * b.span_width * HTML_SCALE);
    int pl = 0;
    for (int i = 0; i < dep_count; ++i) if (deps[i].placed) ++pl;
    double tf = calc_total_flow();

    out << "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"UTF-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=0.5\">\n"
        << "<title>CORELAP #" << run_number << "_" << html_counter << "</title>\n<style>\n"
        << "*{margin:0;padding:0;box-sizing:border-box}body{font-family:'Segoe UI',Arial,sans-serif;background:#d0d0d0;display:flex;flex-direction:column;align-items:center;padding:30px;zoom:.7}"
        << "h1{color:#222;margin-bottom:8px;font-size:60px;font-weight:900}.info{color:#444;margin-bottom:20px;font-size:32px;font-weight:500}"
        << ".canvas{position:relative;background:#f8f8f8;border:5px solid #333;width:" << W << "px;height:" << H << "px;box-shadow:0 8px 40px rgba(0,0,0,.2)}"
        << ".grid-line{position:absolute;left:0;width:100%;height:0;border-top:2px dashed #aaa;pointer-events:none;z-index:1}"
        << ".dep{position:absolute;border:4px solid #333;display:flex;flex-direction:column;align-items:center;justify-content:center;cursor:pointer;z-index:2;overflow:hidden;border-radius:8px;transition:all .15s}"
        << ".dep:hover{opacity:1!important;z-index:100!important;box-shadow:0 12px 40px rgba(0,0,0,.6);transform:scale(1.03)}"
        << ".dep-name{color:white;font-weight:900;font-size:55px;text-align:center;text-shadow:3px 3px 6px rgba(0,0,0,.7);margin-bottom:4px;line-height:1.1}"
        << ".dep-size{color:rgba(255,255,255,.95);font-size:40px;text-align:center;text-shadow:2px 2px 4px rgba(0,0,0,.6);font-weight:500}"
        << ".dep-flow{color:rgba(255,255,255,.9);font-size:35px;text-align:center;text-shadow:2px 2px 3px rgba(0,0,0,.5)}"
        << ".legend{display:flex;gap:30px;margin-top:30px;flex-wrap:wrap;justify-content:center}"
        << ".legend-item{display:flex;align-items:center;gap:12px;font-size:45px;color:#222;font-weight:700}"
        << ".legend-color{width:50px;height:50px;border-radius:10px;border:4px solid #555}"
        << ".tooltip{display:none;position:fixed;background:rgba(0,0,0,.95);color:white;padding:25px 35px;border-radius:18px;font-size:50px;pointer-events:none;z-index:200;line-height:1.5;box-shadow:0 8px 30px rgba(0,0,0,.5);font-weight:500}"
        << ".tooltip b{font-size:55px}</style>\n</head>\n<body>\n<h1>CORELAP - План #" << run_number << "_" << html_counter << "</h1>\n"
        << "<div class=\"info\">Здание: " << std::fixed << std::setprecision(0) << b.max_length << " x " << b.span_count * b.span_width << " м | Пролётов: " << b.span_count << " | Размещено: " << pl << "/" << dep_count << " | Грузопоток: " << std::setprecision(1) << tf << " т*м/год</div>\n"
        << "<div class=\"canvas\">\n";

    for (int i = 1; i < b.span_count; ++i) {
        out << "<div class=\"grid-line\" style=\"top:" << static_cast<int>(i * b.span_width * HTML_SCALE) << "px;\"></div>\n";
    }

    for (int i = 0; i < dep_count; ++i) {
        if (!deps[i].placed) continue;
        int x = static_cast<int>((deps[i].center.x - deps[i].length * 0.5) * HTML_SCALE);
        int y = static_cast<int>((deps[i].center.y - deps[i].width * 0.5) * HTML_SCALE);
        int w = (std::max)(MIN_BLOCK_W, static_cast<int>(deps[i].length * HTML_SCALE));
        int h = (std::max)(MIN_BLOCK_H, static_cast<int>(deps[i].width * HTML_SCALE));

        out << "<div class=\"dep\" style=\"left:" << x << "px;top:" << y << "px;width:" << w << "px;height:" << h << "px;background:" << CATEGORY_COLORS[deps[i].category] << ";opacity:.88\" "
            << "onmouseover=\"showTip(event,'" << deps[i].name << "'," << deps[i].id << "," << deps[i].total_flow << "," << deps[i].total_score << ",'" << CATEGORY_NAMES[deps[i].category] << "'," << deps[i].area << "," << deps[i].width << "," << deps[i].length << "," << deps[i].center.x << "," << deps[i].center.y << ")\" "
            << "onmouseout=\"hideTip()\">\n"
            << "<div class=\"dep-name\">" << deps[i].name << " [" << deps[i].id << "]</div>\n"
            << "<div class=\"dep-size\">" << std::setprecision(1) << deps[i].width << " x " << deps[i].length << " м</div>\n"
            << "<div class=\"dep-flow\">" << std::setprecision(0) << deps[i].area << " m2 | Оценка: " << deps[i].total_score << "</div>\n"
            << "</div>\n";
    }

    out << "</div>\n<div class=\"legend\">\n";
    for (int c = 0; c < MAX_CATEGORIES; ++c) {
        out << "<div class=\"legend-item\"><div class=\"legend-color\" style=\"background:" << CATEGORY_COLORS[c] << ";\"></div>" << CATEGORY_NAMES[c] << " (" << CATEGORY_SCORES[c] << ")</div>\n";
    }
    out << "</div>\n<div class=\"tooltip\" id=\"tip\"></div>\n<script>\n"
        << "function showTip(e,n,i,f,s,c,a,w,l,x,y){var t=document.getElementById('tip');t.innerHTML='<b>'+n+' [ID:'+i+']</b><br>Размер: '+w.toFixed(1)+' x '+l.toFixed(1)+' м<br>Площадь: '+a.toFixed(0)+' m2<br>Поток: '+f.toFixed(1)+' т/г<br>Оценка: '+s+' баллов<br>Категория: '+c+'<br>Центр: ('+x.toFixed(1)+', '+y.toFixed(1)+') м';t.style.display='block';t.style.left=(e.pageX+20)+'px';t.style.top=(e.pageY-40)+'px';}\n"
        << "function hideTip(){document.getElementById('tip').style.display='none';}\n"
        << "</script>\n</body>\n</html>\n";
    out.close();
    std::cout << "План сохранён в " << fn << "\n";
}

// ==================== ЗАПУСКИ ====================

void run_saved() {
    restore_data();
    auto st = high_resolution_clock::now();
    calc_flows_and_scores();
    assign_cat();
    calc_scores();
    std::cout << "\n=== ПЕРЕЗАПУСК ===\n";
    calc_dimensions(saved_building);
    create_grid(saved_building);
    corelap_placement(saved_building);
    auto d = duration_cast<milliseconds>(high_resolution_clock::now() - st).count();
    print_results(d);
    draw_html(saved_building);
    delete[] grid;
    grid = nullptr;
}

void run_new() {
    Building b = input_building();
    input_departments();
    input_flows();
    save_data(b);
    auto st = high_resolution_clock::now();
    calc_flows_and_scores();
    assign_cat();
    calc_scores();
    print_categories();
    calc_dimensions(b);
    create_grid(b);
    corelap_placement(b);
    auto d = duration_cast<milliseconds>(high_resolution_clock::now() - st).count();
    print_results(d);
    draw_html(b);
    delete[] grid;
    grid = nullptr;
}

int main() {
    setlocale(LC_ALL, "Russian");
    std::cout << "===========================================\n   CORELAP - Компоновка цехов (балльная система)\n===========================================\n\n";
    try {
        bool running = true;
        while (running) {
            ++run_number;
            if (!has_saved_data) run_new();
            else {
                std::cout << "\n=== МЕНЮ #" << run_number << " ===\n1. Перезапустить\n2. Новые данные\n3. Инфо\n4. Выход\n";
                switch (read_menu_choice("Выбор: ", 1, 4)) {
                case 1: run_saved(); break;
                case 2: run_new(); break;
                case 3: std::cout << "\nПодразделений: " << saved_dep_count << "\nПролётов: " << saved_building.span_count << " x " << saved_building.span_width << "\nДлина: " << saved_building.max_length << " м\n"; break;
                case 4: running = false; break;
                }
            }
        }
        std::cout << "\nЗапусков: " << run_number << "\nПрограмма завершена.\n";
    }
    catch (...) { delete[] grid; }
    std::cin.get();
    std::cin.get();
}
#include "dark_chess_client.h"
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#define ACTION_DELAY_MS 500

typedef struct {
    int r;
    int c;
    int tr;
    int tc;
} MoveCandidate;

void get_piece_at(const char* json, int index, char* out_piece);
void get_role_color(const char* json, const char* role, char* out_color);

static int is_covered_piece(const char* piece) {
    return strcmp(piece, "Covered") == 0;
}

static int is_null_piece(const char* piece) {
    return strcmp(piece, "Null") == 0;
}

static int is_empty_or_hidden(const char* piece) {
    return is_covered_piece(piece) || is_null_piece(piece);
}

static int is_red_piece(const char* piece) {
    return strncmp(piece, "Red_", 4) == 0;
}

static int is_black_piece(const char* piece) {
    return strncmp(piece, "Black_", 6) == 0;
}

static int piece_rank(const char* piece) {
    if (strstr(piece, "King")) return 6;
    if (strstr(piece, "Guard")) return 5;
    if (strstr(piece, "Elephant")) return 4;
    if (strstr(piece, "Car")) return 3;
    if (strstr(piece, "Horse")) return 2;
    if (strstr(piece, "Cannon")) return 1;
    if (strstr(piece, "Soldier")) return 0;
    return -1;
}

static int piece_is_cannon(const char* piece) {
    return strstr(piece, "Cannon") != NULL;
}

static int piece_is_soldier(const char* piece) {
    return strstr(piece, "Soldier") != NULL;
}

static int piece_is_king(const char* piece) {
    return strstr(piece, "King") != NULL;
}

static int same_color(const char* a, const char* b) {
    return (is_red_piece(a) && is_red_piece(b)) || (is_black_piece(a) && is_black_piece(b));
}

static int is_enemy_piece(const char* piece, const char* my_color) {
    if (is_empty_or_hidden(piece)) return 0;
    if (strcmp(my_color, "Red") == 0) return is_black_piece(piece);
    if (strcmp(my_color, "Black") == 0) return is_red_piece(piece);
    return 0;
}

static int is_my_piece(const char* piece, const char* my_color) {
    if (is_empty_or_hidden(piece)) return 0;
    if (strcmp(my_color, "Red") == 0) return is_red_piece(piece);
    if (strcmp(my_color, "Black") == 0) return is_black_piece(piece);
    return 0;
}

static void load_board(const char* json, char board[4][8][32]) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            get_piece_at(json, r * 8 + c, board[r][c]);
        }
    }
}

static int in_bounds(int r, int c) {
    return r >= 0 && r < 4 && c >= 0 && c < 8;
}

static int can_attack_piece(const char board[4][8][32], int sr, int sc, int tr, int tc) {
    const char* attacker = board[sr][sc];
    const char* target = board[tr][tc];

    if (is_empty_or_hidden(attacker) || is_empty_or_hidden(target)) return 0;
    if (same_color(attacker, target)) return 0;

    int dr = tr - sr;
    int dc = tc - sc;

    if (piece_is_cannon(attacker)) {
        if (sr != tr && sc != tc) return 0;
        int step_r = (dr == 0) ? 0 : (dr > 0 ? 1 : -1);
        int step_c = (dc == 0) ? 0 : (dc > 0 ? 1 : -1);
        int r = sr + step_r;
        int c = sc + step_c;
        int blockers = 0;
        while (r != tr || c != tc) {
            if (!is_null_piece(board[r][c])) blockers++;
            r += step_r;
            c += step_c;
        }
        return blockers == 1;
    }

    if (abs(dr) + abs(dc) != 1) return 0;

    if (piece_is_soldier(attacker) && piece_is_king(target)) return 1;
    if (piece_is_king(attacker) && piece_is_soldier(target)) return 0;

    return piece_rank(attacker) >= piece_rank(target);
}

static int can_move_to(const char board[4][8][32], int sr, int sc, int tr, int tc) {
    const char* mover = board[sr][sc];

    if (is_empty_or_hidden(mover) || !is_null_piece(board[tr][tc])) return 0;
    if (piece_is_cannon(mover)) return abs(sr - tr) + abs(sc - tc) == 1;
    return abs(sr - tr) + abs(sc - tc) == 1;
}

static int square_is_threatened(const char board[4][8][32], const char* my_color, int r, int c) {
    for (int sr = 0; sr < 4; sr++) {
        for (int sc = 0; sc < 8; sc++) {
            if (is_enemy_piece(board[sr][sc], my_color) && can_attack_piece(board, sr, sc, r, c)) {
                return 1;
            }
        }
    }
    return 0;
}

static int find_first_covered(const char board[4][8][32], int* out_r, int* out_c) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (is_covered_piece(board[r][c])) {
                *out_r = r;
                *out_c = c;
                return 1;
            }
        }
    }
    return 0;
}

static int send_flip_action(int r, int c) {
    char action[32];
    snprintf(action, sizeof(action), "%d %d\n", r, c);
    #ifdef _WIN32
    Sleep(ACTION_DELAY_MS);
    #else
    usleep(ACTION_DELAY_MS * 1000);
    #endif
    send_action(action);
    printf("[Action] FLIP %d %d\n", r, c);
    return 1;
}

static int send_move_action(int r, int c, int tr, int tc) {
    char action[32];
    snprintf(action, sizeof(action), "%d %d %d %d\n", r, c, tr, tc);
    #ifdef _WIN32
    Sleep(ACTION_DELAY_MS);
    #else
    usleep(ACTION_DELAY_MS * 1000);
    #endif
    send_action(action);
    printf("[Action] MOVE %d %d -> %d %d\n", r, c, tr, tc);
    return 1;
}

// 輔助函式：從 JSON 中提取 board 陣列中的第 index 個棋子
void get_piece_at(const char* json, int index, char* out_piece) {
    const char* board_start = strstr(json, "\"board\": [[");
    if (!board_start) {
        strcpy(out_piece, "Unknown");
        return;
    }
    
    const char* p = board_start + 11;
    for (int i = 0; i <= index; i++) {
        p = strchr(p, '\"');
        if (!p) break;
        p++;
        const char* end = strchr(p, '\"');
        if (!end) break;
        
        if (i == index) {
            int len = end - p;
            if (len > 31) len = 31;
            strncpy(out_piece, p, len);
            out_piece[len] = '\0';
            return;
        }
        p = end + 1;
    }
    strcpy(out_piece, "Unknown");
}

// 輔助函式：獲取指定角色 (A 或 B) 的顏色 (Red 或 Black)
void get_role_color(const char* json, const char* role, char* out_color) {
    char search_key[20];
    sprintf(search_key, "\"%s\": \"", role);
    const char* p = strstr(json, search_key);
    if (p) {
        p += strlen(search_key);
        const char* end = strchr(p, '\"');
        if (end) {
            int len = end - p;
            strncpy(out_color, p, len);
            out_color[len] = '\0';
            return;
        }
    }
    strcpy(out_color, "None");
}

void make_move(const char* json, const char* my_role_ab) {
    char board[4][8][32];
    char my_color[10], opp_color[10];
    
    // 定義各種動作的候選清單
    MoveCandidate safe_captures[32];
    MoveCandidate escape_moves[64]; // 用來避開危險的移動
    MoveCandidate safe_moves[64];
    
    int safe_capture_count = 0;
    int escape_move_count = 0;
    int safe_move_count = 0;

    get_role_color(json, my_role_ab, my_color);
    srand((unsigned int)time(NULL));
    load_board(json, board);

    // 如果還沒有確定的顏色，只能翻棋
    if (strcmp(my_color, "None") == 0) {
        int flip_r = 0;
        int flip_c = 0;
        if (find_first_covered(board, &flip_r, &flip_c)) {
            send_flip_action(flip_r, flip_c);
        }
        return;
    }

    strcpy(opp_color, strcmp(my_color, "Red") == 0 ? "Black" : "Red");

    // 掃描棋盤上所有自己的棋子
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!is_my_piece(board[r][c], my_color)) continue;

            // 檢查這顆棋子目前是否處於危險之中（會被吃掉）
            int is_currently_threatened = square_is_threatened(board, my_color, r, c);

            // 檢查一般周圍 1 步的移動與攻擊
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (abs(dr) + abs(dc) != 1) continue;
                    int tr = r + dr;
                    int tc = c + dc;
                    if (!in_bounds(tr, tc)) continue;

                    if (can_attack_piece(board, r, c, tr, tc)) {
                        MoveCandidate candidate = {r, c, tr, tc};
                        // 確保吃完後自己不會處於危險
                        if (!square_is_threatened(board, my_color, tr, tc)) {
                            safe_captures[safe_capture_count++] = candidate;
                        }
                    } else if (can_move_to(board, r, c, tr, tc)) {
                        MoveCandidate candidate = {r, c, tr, tc};
                        // 確保移動後的位置是安全的
                        if (!square_is_threatened(board, my_color, tr, tc)) {
                            safe_moves[safe_move_count++] = candidate;
                            // 如果目前有危險，且這步是安全的，列入優先逃脫清單
                            if (is_currently_threatened) {
                                escape_moves[escape_move_count++] = candidate;
                            }
                        }
                    }
                }
            }

            // 針對「炮」的特殊隔山打牛攻擊邏輯
            if (piece_is_cannon(board[r][c])) {
                for (int tr = 0; tr < 4; tr++) {
                    if (tr == r) continue;
                    if (can_attack_piece(board, r, c, tr, c)) {
                        MoveCandidate candidate = {r, c, tr, c};
                        if (!square_is_threatened(board, my_color, tr, c)) {
                            safe_captures[safe_capture_count++] = candidate;
                        }
                    }
                }
                for (int tc = 0; tc < 8; tc++) {
                    if (tc == c) continue;
                    if (can_attack_piece(board, r, c, r, tc)) {
                        MoveCandidate candidate = {r, c, r, tc};
                        if (!square_is_threatened(board, my_color, r, tc)) {
                            safe_captures[safe_capture_count++] = candidate;
                        }
                    }
                }
            }
        }
    }

    // 1. 遇到旁邊可以吃的棋子，優先吃掉 (安全吃子)
    if (safe_capture_count > 0) {
        MoveCandidate chosen = safe_captures[rand() % safe_capture_count];
        send_move_action(chosen.r, chosen.c, chosen.tr, chosen.tc);
        return;
    }

    // 2. 遇到旁邊會被吃的棋子要避開 (優先逃脫)
    if (escape_move_count > 0) {
        MoveCandidate chosen = escape_moves[rand() % escape_move_count];
        send_move_action(chosen.r, chosen.c, chosen.tr, chosen.tc);
        return;
    }

    // 3. 隨意走動 (安全的移動)
    if (safe_move_count > 0) {
        MoveCandidate chosen = safe_moves[rand() % safe_move_count];
        send_move_action(chosen.r, chosen.c, chosen.tr, chosen.tc);
        return;
    }

    // 4. 如果都沒辦法，就翻棋
    int flip_r = 0;
    int flip_c = 0;
    if (find_first_covered(board, &flip_r, &flip_c)) {
        send_flip_action(flip_r, flip_c);
    }
}

int main() {
    char board_data[4000];
    int last_total_moves = -1;

    if (init_connection() != 0) return 1;
    srand((unsigned int)time(NULL));
    auto_join_room();

    char my_role_ab[2] = "";
    if (strcmp(_assigned_role, "first") == 0) strcpy(my_role_ab, "A");
    else if (strcmp(_assigned_role, "second") == 0) strcpy(my_role_ab, "B");

    while (1) {
        receive_update(board_data, 4000);
        printf("%s", board_data);
        if (strlen(board_data) == 0) break;
        if (strstr(board_data, "UPDATE")) {
            // 解析總步數，避免重複處理相同的狀態
            int current_total_moves = -1;
            char* moves_p = strstr(board_data, "\"total_moves\": ");
            if (moves_p) {
                sscanf(moves_p + 15, "%d", &current_total_moves);
            }

            const char* turn_role_p = strstr(board_data, "\"current_turn_role\": \"");
            if (turn_role_p) {
                turn_role_p += 22;
                char current_turn_role[2] = { turn_role_p[0], '\0' };
                
                // 只有在回合匹配且狀態是新的時候才動作
                if (strcmp(current_turn_role, my_role_ab) == 0 && current_total_moves != last_total_moves) {
                    printf("It's my turn (Role %s, Move %d). Thinking...\n", my_role_ab, current_total_moves);
                    make_move(board_data, my_role_ab);
                    last_total_moves = current_total_moves;
                }
            }
        }
    }
    close_connection();
    return 0;
}

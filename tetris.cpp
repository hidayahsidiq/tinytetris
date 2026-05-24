#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>
#include <cctype>
#include <windows.h>
#include <conio.h>

using namespace std;

// ==================== CONSTANTS ====================
const int COLS = 10;
const int ROWS = 20;

// ==================== ANSI ESCAPE CODES ====================
const string RESET        = "\033[0m";
const string BOLD         = "\033[1m";
const string DIM          = "\033[2m";
const string CURSOR_HOME  = "\033[H";
const string CLEAR_SCREEN = "\033[2J\033[H";

// Piece colors (indices 1-7)
const string PCOLORS[] = {
    "",                // 0 (unused)
    "\033[96m",       // 1 I  - Bright Cyan
    "\033[93m",       // 2 O  - Bright Yellow
    "\033[95m",       // 3 T  - Bright Magenta
    "\033[92m",       // 4 S  - Bright Green
    "\033[91m",       // 5 Z  - Bright Red
    "\033[94m",       // 6 J  - Bright Blue
    "\033[38;5;208m", // 7 L  - Orange
};

// ==================== PIECE DEFINITIONS ====================
const vector<string> SHAPES[7] = {
    {"....", "####", "....", "...."},  // I
    {"##", "##"},                       // O
    {".#.", "###", "..."},             // T
    {".##", "##.", "..."},             // S
    {"##.", ".##", "..."},             // Z
    {"#..", "###", "..."},             // J
    {"..#", "###", "..."},             // L
};

const int SHAPE_COLORS[7] = {1, 2, 3, 4, 5, 6, 7};

// ==================== TYPES ====================
struct Piece {
    vector<string> shape;
    int color;
    int row, col;
};

using Board = vector<vector<int>>; // 0=empty, 1-7=block color

// ==================== GAME STATE ====================
Board board(ROWS, vector<int>(COLS, 0));
Piece cur, nxt;
int score = 0, level = 1, totalLines = 0;
bool gameOver = false, paused = false;
mt19937 rng(random_device{}());
vector<int> bag;

// ==================== BAG RANDOMIZER ====================
// Ensures all 7 pieces appear before any repeats
int nextFromBag() {
    if (bag.empty()) {
        bag = {0, 1, 2, 3, 4, 5, 6};
        shuffle(bag.begin(), bag.end(), rng);
    }
    int idx = bag.back();
    bag.pop_back();
    return idx;
}

// ==================== WINDOWS CONSOLE SETUP ====================
void setupConsole() {
    // Enable UTF-8 output
    SetConsoleOutputCP(65001);
    // Enable ANSI escape code processing
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    // Hide cursor
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(h, &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(h, &ci);
}

void showCursor() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(h, &ci);
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(h, &ci);
}

// ==================== PIECE LOGIC ====================
vector<string> rotateCW(const vector<string>& s) {
    int R = (int)s.size(), C = (int)s[0].size();
    vector<string> rot(C, string(R, '.'));
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++)
            rot[c][R - 1 - r] = s[r][c];
    return rot;
}

Piece makePiece(int idx) {
    Piece p;
    p.shape = SHAPES[idx];
    p.color = SHAPE_COLORS[idx];
    p.col = COLS / 2 - (int)p.shape[0].size() / 2;
    p.row = 0;
    // Offset so the first occupied row appears at board row 0
    for (int r = 0; r < (int)p.shape.size(); r++) {
        bool found = false;
        for (char c : p.shape[r]) {
            if (c == '#') { p.row = -r; found = true; break; }
        }
        if (found) break;
    }
    return p;
}

bool collides(const Board& b, const vector<string>& shape, int row, int col) {
    for (int r = 0; r < (int)shape.size(); r++) {
        for (int c = 0; c < (int)shape[r].size(); c++) {
            if (shape[r][c] == '#') {
                int br = row + r, bc = col + c;
                if (bc < 0 || bc >= COLS || br >= ROWS) return true;
                if (br >= 0 && b[br][bc] != 0) return true;
            }
        }
    }
    return false;
}

void lockPiece(const Piece& p) {
    for (int r = 0; r < (int)p.shape.size(); r++) {
        for (int c = 0; c < (int)p.shape[r].size(); c++) {
            if (p.shape[r][c] == '#') {
                int br = p.row + r, bc = p.col + c;
                if (br >= 0 && br < ROWS && bc >= 0 && bc < COLS)
                    board[br][bc] = p.color;
            }
        }
    }
}

int clearFullLines() {
    int cleared = 0;
    for (int r = ROWS - 1; r >= 0; r--) {
        bool full = true;
        for (int c = 0; c < COLS; c++)
            if (board[r][c] == 0) { full = false; break; }
        if (full) {
            cleared++;
            for (int row = r; row > 0; row--)
                board[row] = board[row - 1];
            board[0] = vector<int>(COLS, 0);
            r++; // re-check this row
        }
    }
    return cleared;
}

int getGhostRow() {
    int gr = cur.row;
    while (!collides(board, cur.shape, gr + 1, cur.col))
        gr++;
    return gr;
}

bool tryMove(int dr, int dc) {
    if (!collides(board, cur.shape, cur.row + dr, cur.col + dc)) {
        cur.row += dr;
        cur.col += dc;
        return true;
    }
    return false;
}

bool tryRotate() {
    vector<string> rotated = rotateCW(cur.shape);
    // Try wall kicks: 0, ±1, ±2, plus upward shifts
    int kicks[] = {0, -1, 1, -2, 2};
    for (int kick : kicks) {
        if (!collides(board, rotated, cur.row, cur.col + kick)) {
            cur.shape = rotated;
            cur.col += kick;
            return true;
        }
    }
    // Try shifting up by 1 (helps I-piece near bottom)
    for (int kick : kicks) {
        if (!collides(board, rotated, cur.row - 1, cur.col + kick)) {
            cur.shape = rotated;
            cur.row--;
            cur.col += kick;
            return true;
        }
    }
    return false;
}

void hardDrop() {
    int gr = getGhostRow();
    score += (gr - cur.row) * 2;
    cur.row = gr;
}

void addScore(int cleared) {
    const int pts[] = {0, 100, 300, 500, 800};
    score += pts[cleared] * level;
    totalLines += cleared;
    level = totalLines / 10 + 1;
}

int getDropInterval() {
    return max(800 - (level - 1) * 70, 50);
}

// ==================== DRAWING ====================
void draw() {
    cout << CURSOR_HOME;

    int gr = getGhostRow();

    // Build display grid: positive=filled, negative=ghost, 0=empty
    vector<vector<int>> display(ROWS, vector<int>(COLS, 0));

    // Board
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            display[r][c] = board[r][c];

    // Ghost + current piece (only if game active)
    if (!gameOver) {
        // Ghost piece
        for (int r = 0; r < (int)cur.shape.size(); r++)
            for (int c = 0; c < (int)cur.shape[r].size(); c++)
                if (cur.shape[r][c] == '#') {
                    int br = gr + r, bc = cur.col + c;
                    if (br >= 0 && br < ROWS && bc >= 0 && bc < COLS
                        && display[br][bc] == 0)
                        display[br][bc] = -cur.color;
                }

        // Current piece (overwrites ghost when overlapping)
        for (int r = 0; r < (int)cur.shape.size(); r++)
            for (int c = 0; c < (int)cur.shape[r].size(); c++)
                if (cur.shape[r][c] == '#') {
                    int br = cur.row + r, bc = cur.col + c;
                    if (br >= 0 && br < ROWS && bc >= 0 && bc < COLS)
                        display[br][bc] = cur.color;
                }
    }

    // Build next piece preview (4x4 grid, centered)
    vector<vector<int>> nextGrid(4, vector<int>(4, 0));
    int nrOff = (4 - (int)nxt.shape.size()) / 2;
    int ncOff = (4 - (int)nxt.shape[0].size()) / 2;
    for (int r = 0; r < (int)nxt.shape.size(); r++)
        for (int c = 0; c < (int)nxt.shape[r].size(); c++)
            if (nxt.shape[r][c] == '#')
                nextGrid[nrOff + r][ncOff + c] = nxt.color;

    // === RENDER ===

    // Title
    cout << "            " << BOLD << "\033[96m"
         << "▓▓▓ TETRIS ▓▓▓" << RESET << "\n\n";

    // Top border + NEXT label
    cout << "       ┌────────────┐          " << BOLD
         << "NEXT" << RESET << "\n";

    // Board rows + side panel
    for (int r = 0; r < ROWS; r++) {
        cout << "       │";
        for (int c = 0; c < COLS; c++) {
            if (display[r][c] > 0)
                cout << BOLD << PCOLORS[display[r][c]] << "██" << RESET;
            else if (display[r][c] < 0)
                cout << DIM << PCOLORS[-display[r][c]] << "░░" << RESET;
            else
                cout << "\033[90m··" << RESET;
        }
        cout << "│";

        // Side panel content
        if (r == 0) {
            cout << "          ┌────────┐";
        } else if (r >= 1 && r <= 4) {
            cout << "          │";
            for (int c = 0; c < 4; c++) {
                if (nextGrid[r - 1][c] > 0)
                    cout << BOLD << PCOLORS[nextGrid[r - 1][c]]
                         << "██" << RESET;
                else
                    cout << "  ";
            }
            cout << "│";
        } else if (r == 5) {
            cout << "          └────────┘";
        } else if (r == 7) {
            cout << "     " << BOLD << "SCORE:" << RESET
                 << " " << score;
        } else if (r == 9) {
            cout << "     " << BOLD << "LEVEL:" << RESET
                 << " " << level;
        } else if (r == 11) {
            cout << "     " << BOLD << "LINES:" << RESET
                 << " " << totalLines;
        } else if (r == 14) {
            if (gameOver)
                cout << "   " << BOLD << "\033[91mGAME OVER!\033[0m";
            else if (paused)
                cout << "     " << BOLD << "\033[93mPAUSED\033[0m";
        } else if (r == 15 && gameOver) {
            cout << "   Press R to restart";
        }

        cout << "\n";
    }

    // Bottom border
    cout << "       └────────────┘\n\n";

    // Controls help
    if (!gameOver && !paused) {
        cout << "   \u2190 \u2192 Move    \u2191 Rotate    "
             << "\u2193 Soft Drop\n";
        cout << "   Space: Hard Drop    P: Pause    Q: Quit\n";
    } else if (paused) {
        cout << "   Press P to resume\n";
    }

    cout.flush();
}

// ==================== SPAWNING ====================
void spawn() {
    cur = nxt;
    nxt = makePiece(nextFromBag());
    cur.col = COLS / 2 - (int)cur.shape[0].size() / 2;
    // Position piece so first occupied row is at board top
    for (int r = 0; r < (int)cur.shape.size(); r++) {
        bool found = false;
        for (char c : cur.shape[r]) {
            if (c == '#') { cur.row = -r; found = true; break; }
        }
        if (found) break;
    }
    if (collides(board, cur.shape, cur.row, cur.col))
        gameOver = true;
}

void initGame() {
    board.assign(ROWS, vector<int>(COLS, 0));
    score = 0;
    level = 1;
    totalLines = 0;
    gameOver = false;
    paused = false;
    bag.clear();
    nxt = makePiece(nextFromBag());
    spawn();
    cout << CLEAR_SCREEN;
}

// ==================== MAIN ====================
int main() {
    setupConsole();
    initGame();

    auto lastDrop = chrono::steady_clock::now();

    while (true) {
        // ---- Input ----
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 0 || ch == 0xE0) {       // Special key prefix
                ch = _getch();
                if (!gameOver && !paused) {
                    switch (ch) {
                        case 72:  // Up arrow → rotate
                            tryRotate();
                            break;
                        case 80:  // Down arrow → soft drop
                            if (tryMove(1, 0)) {
                                score++;
                                lastDrop = chrono::steady_clock::now();
                            }
                            break;
                        case 75:  // Left arrow
                            tryMove(0, -1);
                            break;
                        case 77:  // Right arrow
                            tryMove(0, 1);
                            break;
                    }
                }
            } else {                            // Regular key
                switch (tolower(ch)) {
                    case ' ':
                        if (!gameOver && !paused) {
                            hardDrop();
                            lockPiece(cur);
                            {
                                int cl = clearFullLines();
                                if (cl > 0) addScore(cl);
                            }
                            spawn();
                            lastDrop = chrono::steady_clock::now();
                        }
                        break;
                    case 'p':
                        if (!gameOver) paused = !paused;
                        break;
                    case 'q':
                        cout << CLEAR_SCREEN;
                        showCursor();
                        return 0;
                    case 'r':
                        if (gameOver) {
                            initGame();
                            lastDrop = chrono::steady_clock::now();
                        }
                        break;
                }
            }
        }

        // ---- Auto drop ----
        if (!gameOver && !paused) {
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                               now - lastDrop).count();
            if (elapsed >= getDropInterval()) {
                if (!tryMove(1, 0)) {
                    lockPiece(cur);
                    int cl = clearFullLines();
                    if (cl > 0) addScore(cl);
                    spawn();
                }
                lastDrop = chrono::steady_clock::now();
            }
        }

        // ---- Render ----
        draw();

        // ~60 FPS cap to limit CPU usage
        this_thread::sleep_for(chrono::milliseconds(16));
    }

    return 0;
}
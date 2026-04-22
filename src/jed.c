#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include "jed.h"

/* Global text storage */
char *lines[MAX_LINES];
int num_lines = 0;

/* Cursor position */
int cx = 0, cy = 0;

/* Viewport offsets */
int row_offset = 0, col_offset = 0;

/* Editor modes */
Mode mode = NORMAL;
int last_normal_cmd = 0;

/* Current file name */
char current_filename[256] = "out.txt";

/* Search state */
char last_search[256] = "";
int search_direction = 1; /* 1 = forward, -1 = backward */

/* Undo stack */
UndoEntry undo_stack[UNDO_STACK_SIZE];
int undo_top = -1;

/* Operator-motion state */
Operation pending_op = OP_NONE;
int op_start_line = -1;
int op_start_col = -1;
int repeat_count = 0;

/* Terminal settings */
struct termios orig_termios;
static int raw_mode_initialized = 0;
static char *clipboard = NULL;

static int is_word_char(char c) {
    return c != ' ' && c != '\t' && c != '\0';
}

static void set_clipboard(const char *text) {
    free(clipboard);
    if (!text) {
        clipboard = NULL;
        return;
    }
    size_t len = strlen(text) + 1;
    clipboard = malloc(len);
    if (!clipboard) die("malloc");
    memcpy(clipboard, text, len);
}

static void move_to_next_word(void) {
    if (!is_valid_line(cy)) return;
    int line = cy;
    int col = cx;
    int len = (int)strlen(lines[line]);

    if (col < len && is_word_char(lines[line][col])) {
        while (col < len && is_word_char(lines[line][col])) col++;
    }
    while (line < num_lines) {
        len = (int)strlen(lines[line]);
        while (col < len && !is_word_char(lines[line][col])) col++;
        if (col < len) break;
        line++;
        col = 0;
    }
    if (line < num_lines) {
        cy = line;
        cx = col;
    }
}

static void move_to_end_word(void) {
    if (!is_valid_line(cy)) return;
    int line = cy;
    int col = cx;
    int len = (int)strlen(lines[line]);

    if (col >= len) {
        if (line >= num_lines-1) return;
        line++;
        col = 0;
        len = (int)strlen(lines[line]);
    }
    while (col < len && !is_word_char(lines[line][col])) col++;
    while (line < num_lines) {
        while (col < len && is_word_char(lines[line][col])) col++;
        if (col > 0) break;
        line++;
        col = 0;
        len = (line < num_lines) ? (int)strlen(lines[line]) : 0;
    }
    if (line < num_lines) {
        cy = line;
        cx = col > 0 ? col - 1 : 0;
    }
}

static void move_to_prev_word(void) {
    if (!is_valid_line(cy)) return;
    int line = cy;
    int col = cx;
    if (col == 0) {
        if (line == 0) return;
        line--;
        col = (int)strlen(lines[line]);
    } else {
        col--;
    }
    while (line >= 0) {
        while (col > 0 && !is_word_char(lines[line][col])) col--;
        if (is_word_char(lines[line][col])) {
            while (col > 0 && is_word_char(lines[line][col-1])) col--;
            cy = line;
            cx = col;
            return;
        }
        if (line == 0) break;
        line--;
        col = (int)strlen(lines[line]);
    }
}

// ================= Search =================
void search_forward(const char *pattern) {
    if (!pattern || !*pattern) return;
    
    int start_line = cy;
    int start_col = cx + 1; // Start after current position
    
    for (int line = start_line; line < num_lines; line++) {
        if (!is_valid_line(line)) continue;
        char *text = lines[line];
        int col_start = (line == start_line) ? start_col : 0;
        
        char *found = strstr(text + col_start, pattern);
        if (found) {
            cy = line;
            cx = (found - text);
            return;
        }
    }
    
    // Wrap around to beginning
    for (int line = 0; line <= start_line; line++) {
        if (!is_valid_line(line)) continue;
        char *text = lines[line];
        int col_end = (line == start_line) ? start_col : (int)strlen(text);
        
        char *found = strstr(text, pattern);
        if (found && (found - text) < col_end) {
            cy = line;
            cx = (found - text);
            return;
        }
    }
}

void search_backward(const char *pattern) {
    if (!pattern || !*pattern) return;
    
    int start_line = cy;
    int start_col = cx - 1; // Start before current position
    
    for (int line = start_line; line >= 0; line--) {
        if (!is_valid_line(line)) continue;
        char *text = lines[line];
        int col_end = (line == start_line) ? start_col : (int)strlen(text) - 1;
        
        for (int col = col_end; col >= 0; col--) {
            if (strncmp(text + col, pattern, strlen(pattern)) == 0) {
                cy = line;
                cx = col;
                return;
            }
        }
    }
    
    // Wrap around to end
    for (int line = num_lines - 1; line >= start_line; line--) {
        if (!is_valid_line(line)) continue;
        char *text = lines[line];
        int col_start = (line == start_line) ? start_col : 0;
        
        for (int col = (int)strlen(text) - 1; col > col_start; col--) {
            if (strncmp(text + col, pattern, strlen(pattern)) == 0) {
                cy = line;
                cx = col;
                return;
            }
        }
    }
}

// ================= Undo =================
void push_undo(int line, const char *old_text, const char *new_text) {
    if (undo_top >= UNDO_STACK_SIZE - 1) return; // Stack full
    
    undo_top++;
    undo_stack[undo_top].line = line;
    
    if (old_text) {
        size_t len = strlen(old_text) + 1;
        undo_stack[undo_top].old_text = malloc(len);
        if (undo_stack[undo_top].old_text) {
            memcpy(undo_stack[undo_top].old_text, old_text, len);
        }
    } else {
        undo_stack[undo_top].old_text = NULL;
    }
    
    if (new_text) {
        size_t len = strlen(new_text) + 1;
        undo_stack[undo_top].new_text = malloc(len);
        if (undo_stack[undo_top].new_text) {
            memcpy(undo_stack[undo_top].new_text, new_text, len);
        }
    } else {
        undo_stack[undo_top].new_text = NULL;
    }
}

void undo(void) {
    if (undo_top < 0) return;
    
    UndoEntry *entry = &undo_stack[undo_top];
    if (entry->old_text) {
        free(lines[entry->line]);
        lines[entry->line] = entry->old_text;
        entry->old_text = NULL;
    } else if (entry->new_text) {
        // This was a line insertion, need to delete it
        delete_line(entry->line);
    }
    
    free(entry->old_text);
    free(entry->new_text);
    undo_top--;
}

// ================= Operator-motion =================
void execute_operation(Operation op, int start_line, int start_col, int end_line, int end_col) {
    if (op == OP_NONE) return;
    
    // Check if this is a line operation (start_col == 0 and end_col == line length)
    int is_line_op = (start_col == 0 && is_valid_line(start_line) && 
                     end_col == (int)strlen(lines[start_line]));
    
    if (start_line == end_line && !is_line_op) {
        // Single line operation (partial line)
        if (!is_valid_line(start_line)) return;
        
        char *line = lines[start_line];
        int len = (int)strlen(line);
        
        if (op == OP_DELETE || op == OP_CHANGE) {
            // Delete from start_col to end_col
            if (start_col < len && end_col >= start_col) {
                int delete_len = end_col - start_col + 1;
                if (delete_len > len - start_col) delete_len = len - start_col;
                
                // Shift characters left
                for (int i = start_col; i < len - delete_len; i++) {
                    line[i] = line[i + delete_len];
                }
                line[len - delete_len] = '\0';
                
                // Adjust cursor
                if (cx > start_col) {
                    cx = start_col;
                }
            }
        } else if (op == OP_YANK) {
            // Yank from start_col to end_col
            if (start_col < len && end_col >= start_col) {
                int yank_len = end_col - start_col + 1;
                if (yank_len > len - start_col) yank_len = len - start_col;
                
                free(clipboard);
                clipboard = malloc(yank_len + 1);
                if (clipboard) {
                    memcpy(clipboard, line + start_col, yank_len);
                    clipboard[yank_len] = '\0';
                }
            }
        }
        
        if (op == OP_CHANGE) {
            mode = INSERT;
        }
    } else {
        // Multi-line operation or full line operation
        if (op == OP_DELETE) {
            // Delete lines from start_line to end_line
            for (int i = end_line; i >= start_line; i--) {
                delete_line(i);
            }
            if (cy >= num_lines) cy = num_lines - 1;
            if (cy < 0) cy = 0;
            cx = 0; // Move cursor to start of line
        } else if (op == OP_YANK) {
            // Yank the full lines
            if (is_valid_line(start_line)) {
                set_clipboard(lines[start_line]);
            }
        } else if (op == OP_CHANGE) {
            // For change line, delete the line and enter insert mode
            if (is_valid_line(start_line)) {
                delete_line(start_line);
                if (cy >= num_lines) cy = num_lines - 1;
                if (cy < 0) cy = 0;
                cx = 0;
                mode = INSERT;
            }
        }
    }
}

// Move free_lines implementation here, before it's first used
void free_lines() {
    for (int i = 0; i < num_lines; i++) {
        free(lines[i]);
        lines[i] = NULL;
    }
    num_lines = 0;
    free(clipboard);
    clipboard = NULL;
    
    // Clean up undo stack
    for (int i = 0; i <= undo_top; i++) {
        free(undo_stack[i].old_text);
        free(undo_stack[i].new_text);
    }
    undo_top = -1;
}

/* ================= Terminal Handling ================= */
void die(const char *s) {
    perror(s);
    exit(1);
}

void disable_raw_mode() {
    free_lines();  // Add this line
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    if (!raw_mode_initialized) {
        atexit(disable_raw_mode);
        raw_mode_initialized = 1;
    }
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// read a single key, including arrow sequences
int read_key() {
    char c;
    int n = read(STDIN_FILENO, &c, 1);
    if (n != 1) return -1;

    if (c == '\x1b') { // escape sequence
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            // handle typical arrow keys
            if (seq[1] >= '0' && seq[1] <= '9') {
                // extended sequence like "\x1b[3~"
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    if (seq[1] == '3') return 1004; // Delete (forward-delete)
                    // other ~ sequences could be added here
                }
            } else {
                switch(seq[1]) {
                    case 'A': return 1000; // up
                    case 'B': return 1001; // down
                    case 'C': return 1002; // right
                    case 'D': return 1003; // left
                }
            }
        }
        return '\x1b';
    }
    return c;
}

// ================= file I/O =================
void open_file(const char *filename) {
    // Free existing lines first
    free_lines();

    strncpy(current_filename, filename, sizeof(current_filename)-1);
    current_filename[sizeof(current_filename)-1] = '\0';

    FILE *f = fopen(filename, "r");
    if (!f) return;

    char buffer[MAX_COLS];
    while (fgets(buffer, sizeof(buffer), f) && num_lines < MAX_LINES) {
        lines[num_lines] = calloc(MAX_COLS, sizeof(char));
        if (!lines[num_lines]) {
            fclose(f);
            die("calloc");
        }
        strncpy(lines[num_lines], buffer, MAX_COLS-1);

        size_t len = strlen(lines[num_lines]);
        if (len > 0 && lines[num_lines][len-1] == '\n') lines[num_lines][len-1] = '\0';

        num_lines++;
    }
    fclose(f);
}

void save_file(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) { perror("fopen"); return; }
    for (int i=0; i<num_lines; i++) {
        fputs(lines[i], f);
        fputc('\n', f);
    }
    fclose(f);
}

// ================= text editing =================
int is_valid_line(int line) {
    return (line >= 0 && line < num_lines && lines[line] != NULL);
}

void insert_char(int line, int col, char c) {
    if (!is_valid_line(line)) return;
    if (col < 0 || col >= MAX_COLS - 1) return;
    
    int len = strlen(lines[line]);
    if (len >= MAX_COLS - 1) return;  // Ensure space for new char + null terminator
    
    // Push undo entry
    push_undo(line, lines[line], NULL);
    
    // Move characters only if we're not at the end
    if (col <= len) {
        for (int i = len; i >= col; i--) {
            lines[line][i + 1] = lines[line][i];
        }
    } else {
        // If inserting beyond current length, pad with spaces
        for (int i = len; i < col; i++) {
            lines[line][i] = ' ';
        }
        lines[line][col + 1] = '\0';
    }
    
    lines[line][col] = c;
    lines[line][MAX_COLS - 1] = '\0';  // Ensure null termination
}

void delete_char(int line, int col) {
    if (!is_valid_line(line)) return;
    int len = strlen(lines[line]);
    if (col >= len) return;
    
    // Push undo entry
    push_undo(line, lines[line], NULL);
    
    for (int i=col; i<len; i++) lines[line][i] = lines[line][i+1];
}

void insert_line(int index) {
    if (num_lines >= MAX_LINES) return;
    
    // Push undo entry for line insertion
    push_undo(index, NULL, "");
    
    for (int i=num_lines; i>index; i--) lines[i] = lines[i-1];
    lines[index] = calloc(MAX_COLS, sizeof(char));
    if (!lines[index]) die("calloc");
    num_lines++;
}

void delete_line(int index) {
    if (!is_valid_line(index)) return;
    
    // Push undo entry
    push_undo(index, lines[index], NULL);
    
    free(lines[index]);
    for (int i=index; i<num_lines-1; i++) lines[i] = lines[i+1];
    num_lines--;
    if (num_lines >= 0 && num_lines < MAX_LINES) lines[num_lines] = NULL;
    if (cy >= num_lines) cy = num_lines - 1;
    if (cy < 0) cy = 0;
    if (is_valid_line(cy) && cx > (int)strlen(lines[cy])) cx = (int)strlen(lines[cy]);
}

// ================= screen =================
void get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)==-1) {
        *rows = 24; *cols = 80;
    } else {
        *rows = ws.ws_row; *cols = ws.ws_col;
    }
}

// ensure cursor is inside viewport
void scroll() {
    int rows, cols;
    get_window_size(&rows, &cols);
    int visible_rows = rows - 2; // reserve status + command lines
    if (visible_rows < 1) visible_rows = 1;

    // vertical
    if (cy < row_offset) row_offset = cy;
    else if (cy >= row_offset + visible_rows) row_offset = cy - visible_rows + 1;

    // ensure row_offset bounds
    if (row_offset < 0) row_offset = 0;
    if (num_lines > visible_rows) {
        int max_row_offset = num_lines - visible_rows;
        if (row_offset > max_row_offset) row_offset = max_row_offset;
    } else {
        row_offset = 0;
    }

    // horizontal
    if (cx < col_offset) col_offset = cx;
    else if (cx >= col_offset + cols) col_offset = cx - cols + 1;
    if (col_offset < 0) col_offset = 0;
}

// Updated refresh_screen: render only the viewport slice
void refresh_screen() {
    int rows, cols;
    get_window_size(&rows, &cols);

    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    int visible_rows = rows - 2; // reserve status + command
    if (visible_rows < 0) visible_rows = 0;

    for (int i = 0; i < visible_rows; i++) {
        int file_line = row_offset + i;
        if (file_line < num_lines && lines[file_line]) {
            size_t len = strlen(lines[file_line]);
            if (col_offset < (int)len) {
                size_t avail = len - col_offset;
                size_t to_write = (avail > (size_t)cols) ? (size_t)cols : avail;
                write(STDOUT_FILENO, lines[file_line] + col_offset, to_write);
            }
        }
        write(STDOUT_FILENO, "\r\n", 2);
    }

    // status line second to last
    char status[512];
    const char *mode_str;
    if (mode == INSERT) mode_str = "INSERT";
    else if (mode == COMMAND) mode_str = "COMMAND";
    else if (mode == SEARCH) mode_str = "SEARCH";
    else mode_str = "NORMAL";
    
    snprintf(status, sizeof(status), "-- %s --   %s",
             mode_str,
             current_filename);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;1H", rows-1);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, status, strlen(status));

    // clear last line for command input
    snprintf(buf, sizeof(buf), "\x1b[%d;1H", rows);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[K", 3); // clear line

    // move cursor to editing position (relative to viewport)
    int screen_row = (cy - row_offset) + 1;
    int screen_col = (cx - col_offset) + 1;
    if (screen_row < 1) screen_row = 1;
    if (screen_col < 1) screen_col = 1;
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", screen_row, screen_col);
    write(STDOUT_FILENO, buf, strlen(buf));
}

// ================= Input Handlers =================
void handle_insert(int c) {
    if (c==27) { mode=NORMAL; }
    else if (c==1000) {
        if (cy > 0) {
            cy--;
            if (is_valid_line(cy)) {
                int len = (int)strlen(lines[cy]);
                if (cx > len) cx = len;
            } else {
                cx = 0;
            }
        }
    }
    else if (c==1001) {
        if (cy < num_lines-1) {
            cy++;
            if (is_valid_line(cy)) {
                int len = (int)strlen(lines[cy]);
                if (cx > len) cx = len;
            } else {
                cx = 0;
            }
        }
    }
    else if (c==1002) {
        if (is_valid_line(cy)) {
            int len = (int)strlen(lines[cy]);
            if (cx < len) cx++;
        }
    }
    else if (c==1003) { if (cx>0) cx--; }
    else if (c==1004) {
        if (is_valid_line(cy)) {
            int len = (int)strlen(lines[cy]);
            if (cx < len) delete_char(cy, cx);
        }
    }
    else if (c=='\r') { insert_line(cy+1); cx = 0; cy++; }
    else if (c==127 || c=='\b') {
        if (cx>0 && is_valid_line(cy)) {
            delete_char(cy, cx-1);
            cx--;
        }
    }
    else if (c>=32 && c<=126) {
        if (is_valid_line(cy)) {
            insert_char(cy, cx, c);
            cx++;
        }
    }
}

void handle_normal(int c) {
    // Handle pending operator first
    if (pending_op != OP_NONE) {
        int end_line = cy;
        int end_col = cx;
        
        // Check for line operations (dd, yy, cc)
        if ((pending_op == OP_DELETE && c == 'd') ||
            (pending_op == OP_YANK && c == 'y') ||
            (pending_op == OP_CHANGE && c == 'c')) {
            // Line operation
            end_line = cy;
            end_col = (int)strlen(lines[cy]);
        } else {
            // Handle motion after operator
            if (c == 'w') {
                move_to_next_word();
                end_line = cy;
                end_col = cx - 1; // Include the character we're on
            } else if (c == 'b') {
                move_to_prev_word();
                end_line = cy;
                end_col = cx;
            } else if (c == 'e') {
                move_to_end_word();
                end_line = cy;
                end_col = cx;
            } else if (c == 'h' || c == 1003) {
                if (cx > 0) cx--;
                end_col = cx;
            } else if (c == 'l' || c == 1002) {
                if (is_valid_line(cy)) {
                    int len = (int)strlen(lines[cy]);
                    if (cx < len - 1) cx++;
                }
                end_col = cx;
            } else if (c == '$') {
                if (is_valid_line(cy)) cx = (int)strlen(lines[cy]);
                end_col = cx;
            } else if (c == '0') {
                cx = 0;
                end_col = cx;
            } else {
                // Invalid motion, cancel operation
                pending_op = OP_NONE;
                return;
            }
        }
        
        // Execute the operation
        execute_operation(pending_op, op_start_line, op_start_col, end_line, end_col);
        pending_op = OP_NONE;
        return;
    }
    
    // Handle repeat count
    if (c >= '0' && c <= '9') {
        repeat_count = repeat_count * 10 + (c - '0');
        return;
    }
    
    // Apply repeat count (default to 1 if none specified)
    int count = (repeat_count > 0) ? repeat_count : 1;
    repeat_count = 0;
    
    // Handle commands
    if (c==':') { 
        mode = COMMAND; 
        return; 
    }
    else if (c=='/') { 
        mode = SEARCH; 
        search_direction = 1; 
        return; 
    }
    else if (c=='h' || c==1003) {
        for (int i = 0; i < count; i++) {
            if (cx > 0) cx--;
        }
    }
    else if (c=='j' || c==1001) {
        for (int i = 0; i < count; i++) {
            if (cy < num_lines-1) {
                cy++;
                if (is_valid_line(cy)) {
                    int len = (int)strlen(lines[cy]);
                    if (cx > len) cx = len;
                } else {
                    cx = 0;
                }
            }
        }
    }
    else if (c=='k' || c==1000) {
        for (int i = 0; i < count; i++) {
            if (cy > 0) {
                cy--;
                if (is_valid_line(cy)) {
                    int len = (int)strlen(lines[cy]);
                    if (cx > len) cx = len;
                } else {
                    cx = 0;
                }
            }
        }
    }
    else if (c=='l' || c==1002) {
        for (int i = 0; i < count; i++) {
            if (is_valid_line(cy)) {
                int len = (int)strlen(lines[cy]);
                if (cx < len) cx++;
            }
        }
    }
    else if (c=='0') {
        cx = 0;
    }
    else if (c=='$') {
        if (is_valid_line(cy)) cx = (int)strlen(lines[cy]);
    }
    else if (c=='x') {
        for (int i = 0; i < count; i++) {
            if (is_valid_line(cy)) {
                int len = (int)strlen(lines[cy]);
                if (cx < len) delete_char(cy, cx);
            }
        }
    }
    else if (c=='d') {
        if (count == 1) {
            // Start delete operation
            pending_op = OP_DELETE;
            op_start_line = cy;
            op_start_col = cx;
        } else {
            // Multi-line delete (e.g., 3dd)
            for (int i = 0; i < count && cy < num_lines; i++) {
                delete_line(cy);
            }
            if (cy >= num_lines) cy = num_lines - 1;
            if (cy < 0) cy = 0;
        }
    }
    else if (c=='y') {
        if (count == 1) {
            // Start yank operation
            pending_op = OP_YANK;
            op_start_line = cy;
            op_start_col = cx;
        } else {
            // Multi-line yank (e.g., 3yy)
            if (is_valid_line(cy)) {
                set_clipboard(lines[cy]);
            }
        }
    }
    else if (c=='c') {
        if (count == 1) {
            // Start change operation
            pending_op = OP_CHANGE;
            op_start_line = cy;
            op_start_col = cx;
        } else {
            // For now, treat as single line change
            pending_op = OP_CHANGE;
            op_start_line = cy;
            op_start_col = cx;
        }
    }
    else if (c=='p') {
        if (clipboard) {
            for (int i = 0; i < count; i++) {
                insert_line(cy + 1);
                if (is_valid_line(cy+1)) {
                    strncpy(lines[cy+1], clipboard, MAX_COLS-1);
                    lines[cy+1][MAX_COLS-1] = '\0';
                }
                cy++;
                cx = 0;
            }
        }
    }
    else if (c=='P') {
        if (clipboard) {
            for (int i = 0; i < count; i++) {
                insert_line(cy);
                if (is_valid_line(cy)) {
                    strncpy(lines[cy], clipboard, MAX_COLS-1);
                    lines[cy][MAX_COLS-1] = '\0';
                }
                cx = 0;
            }
        }
    }
    else if (c=='w') {
        for (int i = 0; i < count; i++) {
            move_to_next_word();
        }
    }
    else if (c=='b') {
        for (int i = 0; i < count; i++) {
            move_to_prev_word();
        }
    }
    else if (c=='e') {
        for (int i = 0; i < count; i++) {
            move_to_end_word();
        }
    }
    else if (c=='n') {
        for (int i = 0; i < count; i++) {
            if (search_direction == 1) {
                search_forward(last_search);
            } else {
                search_backward(last_search);
            }
        }
    }
    else if (c=='N') {
        for (int i = 0; i < count; i++) {
            if (search_direction == 1) {
                search_backward(last_search);
            } else {
                search_forward(last_search);
            }
        }
    }
    else if (c=='u') {
        for (int i = 0; i < count; i++) {
            undo();
        }
    }
    else if (c=='o') {
        for (int i = 0; i < count; i++) {
            if (cy >= 0 && cy < num_lines) {
                insert_line(cy + 1);
                cy++;
                cx = 0;
                mode = INSERT;
                // Only do first one, then break
                break;
            }
        }
    }
    else if (c=='a') {
        if (is_valid_line(cy)) {
            int len = (int)strlen(lines[cy]);
            if (cx < len) cx++;
            mode = INSERT;
        }
    }
    else if (c=='i') {
        mode = INSERT;
    }
}

void handle_command() {
    int rows, cols;
    get_window_size(&rows,&cols);

    char cmd[256]; // Increased buffer size to handle filename

    // move to last row and show colon
    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;1H",rows);
    write(STDOUT_FILENO,buf,strlen(buf));
    write(STDOUT_FILENO, ":",1);

    // temporarily enable canonical + echo
    struct termios raw, orig=orig_termios;
    raw=orig;
    raw.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    if (fgets(cmd,sizeof(cmd),stdin)==NULL) cmd[0]='\0';

    size_t len = strlen(cmd);
    if (len>0 && cmd[len-1]=='\n') cmd[len-1]='\0';

    // restore raw
    enable_raw_mode();

    // Parse command and optional filename
    char command[32] = {0};
    char filename[224] = {0};
    sscanf(cmd, "%s %s", command, filename);

    if (strcmp(command,"q")==0) exit(0);
    else if (strcmp(command,"w")==0) {
        if (filename[0] != '\0') {
            strncpy(current_filename, filename, sizeof(current_filename)-1);
            current_filename[sizeof(current_filename)-1] = '\0';
        }
        save_file(current_filename);
    }
    else if (strcmp(command,"wq")==0) { 
        if (filename[0] != '\0') {
            strncpy(current_filename, filename, sizeof(current_filename)-1);
            current_filename[sizeof(current_filename)-1] = '\0';
        }
        save_file(current_filename);
        exit(0);
    }
    else if (strcmp(command,"e")==0) {
        if (filename[0] != '\0') {
            open_file(filename);
            if (num_lines == 0) insert_line(0);
        }
    }

    mode = NORMAL;
}

void handle_search() {
    int rows, cols;
    get_window_size(&rows,&cols);

    char search_str[256];

    // move to last row and show search prompt
    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;1H",rows);
    write(STDOUT_FILENO,buf,strlen(buf));
    write(STDOUT_FILENO, search_direction == 1 ? "/" : "?", 1);

    // temporarily enable canonical + echo
    struct termios raw, orig=orig_termios;
    raw=orig;
    raw.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    if (fgets(search_str,sizeof(search_str),stdin)==NULL) search_str[0]='\0';

    size_t len = strlen(search_str);
    if (len>0 && search_str[len-1]=='\n') search_str[len-1]='\0';

    // restore raw
    enable_raw_mode();

    // Perform search
    if (search_str[0] != '\0') {
        strncpy(last_search, search_str, sizeof(last_search)-1);
        last_search[sizeof(last_search)-1] = '\0';
        
        if (search_direction == 1) {
            search_forward(search_str);
        } else {
            search_backward(search_str);
        }
    }

    mode = NORMAL;
}

#include "aux.c"

#define MAX_LINES 10000
#define MAX_COLS 1024

// Global text storage
char *lines[MAX_LINES];
int num_lines = 0;

// Cursor position
int cx = 0, cy = 0;
// Viewport offsets
int row_offset = 0, col_offset = 0;

// Editor modes
typedef enum { NORMAL, INSERT, COMMAND } Mode;
Mode mode = NORMAL;

// Current file name
char current_filename[256] = "out.txt";

// Terminal settings
struct termios orig_termios;

// ================= Terminal Handling =================
void die(const char *s) {
    perror(s);
    exit(1);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
}

void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disable_raw_mode);
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
    FILE *f = fopen(filename, "r");
    if (!f) return;

    strncpy(current_filename, filename, sizeof(current_filename)-1);

    char buffer[MAX_COLS];
    while (fgets(buffer, sizeof(buffer), f)) {
        lines[num_lines] = calloc(MAX_COLS, sizeof(char));
        if (!lines[num_lines]) die("calloc");
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
void insert_char(int line, int col, char c) {
    if (line >= num_lines) return;
    int len = strlen(lines[line]);
    if (len+1 >= MAX_COLS) return;
    for (int i=len; i>=col; i--) lines[line][i+1] = lines[line][i];
    lines[line][col] = c;
}

void delete_char(int line, int col) {
    int len = strlen(lines[line]);
    if (col >= len) return;
    for (int i=col; i<len; i++) lines[line][i] = lines[line][i+1];
}

void insert_line(int index) {
    if (num_lines >= MAX_LINES) return;
    for (int i=num_lines; i>index; i--) lines[i] = lines[i-1];
    lines[index] = calloc(MAX_COLS, sizeof(char));
    if (!lines[index]) die("calloc");
    num_lines++;
}

void delete_line(int index) {
    free(lines[index]);
    for (int i=index; i<num_lines-1; i++) lines[i] = lines[i+1];
    num_lines--;
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
    char status[256];
    snprintf(status, sizeof(status), "-- %s --   %s",
             (mode==INSERT)?"INSERT":(mode==COMMAND)?"COMMAND":"NORMAL",
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
    else if (c==1000) { if (cy>0) cy--; cx= cx>strlen(lines[cy])?strlen(lines[cy]):cx; }
    else if (c==1001) { if (cy<num_lines-1) cy++; cx= cx>strlen(lines[cy])?strlen(lines[cy]):cx; }
    else if (c==1002) { if (cx<strlen(lines[cy])) cx++; }
    else if (c==1003) { if (cx>0) cx--; }
    else if (c==1004) { // Delete key: remove character under cursor
        if (cx < (int)strlen(lines[cy])) delete_char(cy, cx);
    }
    else if (c=='\r') { insert_line(cy+1); cx=0; cy++; }
    else if (c==127 || c=='\b') { if (cx>0) { delete_char(cy,cx-1); cx--; } }
    else if (c>=32 && c<=126) { insert_char(cy,cx,c); cx++; }
}

void handle_normal(int c) {
    if (c==':') { mode=COMMAND; return; }
    else if (c==1000) { if (cy>0) cy--; cx= cx>strlen(lines[cy])?strlen(lines[cy]):cx; }
    else if (c==1001) { if (cy<num_lines-1) cy++; cx= cx>strlen(lines[cy])?strlen(lines[cy]):cx; }
    else if (c==1002) { if (cx<strlen(lines[cy])) cx++; }
    else if (c==1003) { if (cx>0) cx--; }
    else if (c==1004) { // Delete key in normal mode: delete char under cursor
        if (cx < (int)strlen(lines[cy])) delete_char(cy, cx);
    }
    else if (c=='i') { mode=INSERT; }
}

void handle_command() {
    int rows, cols;
    get_window_size(&rows,&cols);

    char cmd[64];
    int i=0;

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

    if (strcmp(cmd,"q")==0) exit(0);
    else if (strcmp(cmd,"w")==0) save_file(current_filename);
    else if (strcmp(cmd,"wq")==0) { save_file(current_filename); exit(0); }

    mode=NORMAL;
}

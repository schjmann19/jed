#ifndef JED_H
#define JED_H

#include <termios.h>

#define MAX_LINES 10000
#define MAX_COLS 1024
#define UNDO_STACK_SIZE 100
#define loop for(;;)

/* Editor modes */
typedef enum { NORMAL, INSERT, COMMAND, SEARCH } Mode;

/* Operations for operator-motion */
typedef enum { OP_NONE, OP_DELETE, OP_YANK, OP_CHANGE } Operation;

/* Undo entry */
typedef struct {
    int line;
    char *old_text;
    char *new_text;
} UndoEntry;

/* Global text storage */
extern char *lines[MAX_LINES];
extern int num_lines;

/* Cursor position */
extern int cx, cy;

/* Viewport offsets */
extern int row_offset, col_offset;

/* Editor mode */
extern Mode mode;

/* Current file name */
extern char current_filename[256];

/* Terminal settings */
extern struct termios orig_termios;

/* Terminal handling */
void die(const char *s);
void enable_raw_mode(void);
void disable_raw_mode(void);
void free_lines(void);

/* Key input */
int read_key(void);

/* File I/O */
void open_file(const char *filename);
void save_file(const char *filename);

/* Text editing */
int is_valid_line(int line);
void insert_char(int line, int col, char c);
void delete_char(int line, int col);
void insert_line(int index);
void delete_line(int index);

/* Search */
void search_forward(const char *pattern);
void search_backward(const char *pattern);

/* Undo */
void push_undo(int line, const char *old_text, const char *new_text);
void undo(void);

/* Operator-motion */
void execute_operation(Operation op, int start_line, int start_col, int end_line, int end_col);

/* Screen rendering */
void get_window_size(int *rows, int *cols);
void scroll(void);
void refresh_screen(void);

/* Input handlers */
void handle_insert(int c);
void handle_normal(int c);
void handle_command(void);
void handle_search(void);

#endif /* JED_H */

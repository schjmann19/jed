#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jed.h"
#include "aux.h"

int main(int argc, char **argv) 
{
    int opt;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) { help(); return 0; }
        if (strcmp(argv[i], "--version") == 0) { ver(); return 0; }
    }

    while ((opt = getopt(argc, argv, "hv")) != -1) {
        switch (opt) {
            case 'h': help(); return 0;
            case 'v': ver(); return 0;
            default: bad_arg();
        }
    }

    // enter editor
    enable_raw_mode();
    if (optind < argc) open_file(argv[optind]);
    if (num_lines == 0) insert_line(0);

    loop {
        // if entering command mode, handle it immediately
        if (mode==COMMAND) { handle_command(); continue; }
        if (mode==SEARCH) { handle_search(); continue; }

        // ensure viewport follows cursor before rendering
        scroll();
        if (mode!=COMMAND && mode!=SEARCH) refresh_screen();

        int c = read_key();
        if (c==-1) continue;

        if (mode==INSERT) handle_insert(c);
        else if (mode==NORMAL) handle_normal(c);
        else if (mode==COMMAND) handle_command();
        else if (mode==SEARCH) handle_search();
    }

    return 0;
}

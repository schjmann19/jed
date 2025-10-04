#include "jed.c"

int main(int argc, char **argv) 
{
    int opt;
    static struct option longopts[] = {
        {"help",    no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0,0,0,0}
    };

    while ((opt = getopt_long(argc, argv, "hv", longopts, NULL)) != -1) {
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

    while (1) {
        // if entering command mode, handle it immediately
        if (mode==COMMAND) { handle_command(); continue; }

        // ensure viewport follows cursor before rendering
        scroll();
        if (mode!=COMMAND) refresh_screen();

        int c = read_key();
        if (c==-1) continue;

        if (mode==INSERT) handle_insert(c);
        else if (mode==NORMAL) handle_normal(c);
        else if (mode==COMMAND) handle_command();
    }

    return 0;
}

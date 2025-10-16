#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

void bad_arg(void){
    fprintf(stderr, "bad argument\n use -h or --help for help\n");
    exit(1);
}

void ver(void){
    printf("jed version 0.1\n");
    printf("by Jimena Neumann (schjmann19@gmail.com)\n");
}

void help(void){
    ver();
    printf("a bare-bones text editor written in C\n\n");
    printf("Use :w to save, :q to quit, :wq to write and quit\n\n");
    printf("Usage:\n");
    printf("  jed [filename]\n\n");
    printf("or\n\n");
    printf("  jed [options]\n\n");
    printf("Options:\n");
    printf("  -h, --help           Show this help and exit\n");
    printf("  -v, --version        Show version and exit\n");
}

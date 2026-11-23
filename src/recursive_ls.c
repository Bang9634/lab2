#include "list_dir.h"
#include <locale.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [DIRECTORY]\n", argv[0]);
        return 2;
    }
    setlocale(LC_COLLATE, "");
    return list_dir(argc == 2 ? argv[1] : ".", 1);
}

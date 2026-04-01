#include <string.h>

int is_safe_path(const char *path) {
    if (path == NULL) {
        return 0;
    }

    if (strstr(path, "..") != NULL) {
        return 0;
    }

    return 1;
}//
// Created by Fereshteh on 3/30/26.
//
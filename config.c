#include "config.h"

#include <stdio.h>
#include <string.h>

int read_config(const char* filedir, config_t* cfg) {
    FILE* fp = fopen(filedir, "r"); //reads config.txt
    if (!fp) return -1;

    char line[64];

    while (fgets(line, sizeof(line), fp)) {
        char* key = strtok(line, "= \t\n");
        char* value = strtok(NULL, "= \t\n");

        if (!key || !value) continue;

        if (strcmp(key, "TRIAGE_QUEUE_MAX") == 0)
            cfg->TRIAGE_QUEUE_MAX = atoi(value);
        else if (strcmp(key, "TRIAGE") == 0)
            cfg->TRIAGE_THREADS = atoi(value);
        else if (strcmp(key, "DOCTORS") == 0)
            cfg->DOCTORS = atoi(value);
        else if (strcmp(key, "SHIFT_LENGTH") == 0)
            cfg->SHIFT_LENGTH = atoi(value);
        else if (strcmp(key, "MSQ_WAIT_MAX") == 0)
            cfg->MSQ_WAIT_MAX = atoi(value);
        else
            return -1;
    }

    for(int i = 0; i < 5; ++i)
        if (*((int*)cfg + i) < 1) return -1;

    fclose(fp);
    return 0;
}
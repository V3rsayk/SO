#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "structs.h"
#include "config.h"

const char* names[] = {
    "Alice", "Bob", "Charlie", "Diana", "Eve",
    "Frank", "Grace", "Hank", "Ivy", "Jack"
};

int main(int argc, char* argv[]) {
    int count = (argc > 1) ? atoi(argv[1]) : 20;

    int fd = open(PIPE_NAME, O_WRONLY);
    if (fd == -1) {
        printf("Failed to open pipe\n");
        exit(1);
    }

    srand(time(NULL));

    for (int i = 0; i < count; i++) {
        patient_t p;
        memset(&p, 0, sizeof(patient_t));

        p.arrival_number = i + 1;
        snprintf(p.name, NAME_LEN, "%s_%d", names[rand() % 10], i);

        p.triage_time_ms = (rand() % 3000) + 500;
        p.treatment_time_ms = (rand() % 5000) + 1000;
        p.priority = rand() % 5 + 1;
        p.arrival_time = time(NULL);

        // sigpipe terminates process if pipe is closed
        ssize_t w = write(fd, &p, sizeof(patient_t));
        if (w != sizeof(patient_t)) {
            printf("Failed to write patient\n");
            break;
        }

        printf("Sent patient: %s (arrival=%d, priority=%d)\n",
               p.name, p.arrival_number, p.priority);

        int delay_ms = (rand() % 200) + 50;
        usleep(delay_ms * 1000);
    }

    close(fd);
    return 0;
}

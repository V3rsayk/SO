#define PIPE_NAME   "input_pipe"
#define CONFIG_NAME "config.txt"

typedef struct {
    int TRIAGE_QUEUE_MAX;
    int TRIAGE_THREADS;
    int DOCTORS;
    int SHIFT_LENGTH;
    int MSQ_WAIT_MAX;
} config_t;

int read_config(const char*, config_t*);
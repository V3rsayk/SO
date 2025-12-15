#include <pthread.h>
#include <time.h>

#define NAME_LEN 50

typedef struct {
    int arrival_number;
    char name[NAME_LEN];
    int triage_time_ms;
    int treatment_time_ms;
    int priority;

    time_t arrival_time;
    time_t triage_start;
    time_t triage_end;
    time_t treatment_start;
    time_t treatment_end;
} patient_t;

typedef struct {
    long msgtype;
    patient_t patient;
} mq_message_t;

typedef struct {
    int total_triaged;
    int total_treated;
    long total_wait_triage_ms;
    long total_wait_treatment_ms;
    long total_time_ms;
} shm_stats_t;

// https://en.wikipedia.org/wiki/Circular_buffer
typedef struct {
    patient_t* buffer;
    int capacity;
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    // pthread_cond_t not_full; // not needed if we are printing error when queue is full
} triage_queue_t;
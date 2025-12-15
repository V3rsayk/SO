#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/select.h>

#include "config.h"
#include "log.h"
#include "structs.h"

int input_pipe = -1;
int msqid = -1;
shm_stats_t* shm_stats = NULL;
int shmid = -1;

triage_queue_t queue;
pthread_t* triage_threads = NULL;
pid_t* doctor_pids = NULL;
time_t* doctor_start_times = NULL;

void cleanup() {
    log_message("\nCleaning up resources...\n");

    close(input_pipe);
    unlink(PIPE_NAME);
    msgctl(msqid, IPC_RMID, NULL);
    shmdt(shm_stats);
    shmctl(shmid, IPC_RMID, NULL);

    free(queue.buffer);
    free(triage_threads);
    free(doctor_pids);
    free(doctor_start_times);

    log_message("Resources cleaned up successfully.\n");
}

void handler_sigint(int sig) {
    log_message("\nTerminating process...\n");
    cleanup();
    exit(0);
}

void handler_sigusr1(int sig) {
    log_message("\n--- Statistics ---\n");
    log_message("Total patients triaged: %d\n", shm_stats->total_triaged);
    log_message("Total patients treated: %d\n", shm_stats->total_treated);
    log_message("Average wait time before triage: %ld ms\n", shm_stats->total_wait_triage_ms / shm_stats->total_triaged);
    log_message("Average wait time before treatment: %ld ms\n", shm_stats->total_wait_treatment_ms / shm_stats->total_treated);
    log_message("Total time spent in system: %ld ms\n", shm_stats->total_time_ms);
}

void* triage_worker(void* arg) {
    for(;;) {
        pthread_mutex_lock(&queue.mutex);

        while (queue.count == 0) {
            pthread_cond_wait(&queue.not_empty, &queue.mutex);
        }

        patient_t patient = queue.buffer[queue.head];
        queue.head = (queue.head + 1) % queue.capacity;
        queue.count--;

        pthread_mutex_unlock(&queue.mutex);

        log_message("Processing patient '%s' with arrival number: %d\n", patient.name, patient.arrival_number);

        usleep(patient.triage_time_ms * 1000);

        mq_message_t msg;
        msg.msgtype = (long)patient.priority;
        msg.patient = patient;

        if (msgsnd(msqid, &msg, sizeof(patient_t), 0) == -1)
            log_message("msgsnd failed\n");
        else
            log_message("Patient '%s' pushed to message queue with priority %ld\n", patient.name, msg.msgtype);
    }

    return NULL;
}

void doctor_process(int doctor_id) {
    mq_message_t msg;
    while (1) {
        ssize_t ret = msgrcv(msqid, &msg, sizeof(patient_t), doctor_id, 0);

        if (ret == -1) {
            if (errno == ENOMSG) {
                usleep(100 * 1000);
                continue;
            } else if (errno == EINTR) {
                continue;
            } else {
                perror("msgrcv failed");
                break;
            }
        }

        if (msg.msgtype == doctor_id) {
            log_message("Doctor %d received termination message. Exiting...\n", doctor_id);
            break;
        }

        patient_t patient = msg.patient;
        log_message("Doctor %d treating patient '%s' (priority %d)\n", doctor_id, patient.name, patient.priority);


        time_t treatment_start = time(NULL);
        shm_stats->total_triaged++;
        shm_stats->total_wait_triage_ms += (treatment_start - patient.arrival_time);
        
        usleep(patient.treatment_time_ms * 1000);

        time_t treatment_end = time(NULL);
        shm_stats->total_treated++;
        shm_stats->total_wait_treatment_ms += (treatment_end - treatment_start);
        shm_stats->total_time_ms += (treatment_end - patient.arrival_time);

        log_message("Doctor %d finished treating patient '%s'\n", doctor_id, patient.name);
    }

    exit(0);
}

int main(int argc, const char* argv[]) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    log_message("SIGINT and SIGUSR1 have been blocked\n");

    unlink(PIPE_NAME);
    if (mkfifo(PIPE_NAME, 0666) == -1) {
        log_message("Failed to create input pipe\n");
        cleanup();
        exit(1);
    }

    input_pipe = open(PIPE_NAME, O_RDWR);
    if (input_pipe == -1) {
        log_message("Failed to open input pipe\n");
        cleanup();
        exit(1);
    }
    log_message("Input pipe created with id: %d\n", input_pipe);

    config_t cfg;
    if (read_config(CONFIG_NAME, &cfg) == -1) {
        log_message("Failed to read input file\n");
        cleanup();
        exit(1);
    }

    shmid = shmget(IPC_PRIVATE, sizeof(shm_stats_t), IPC_CREAT | 0666);
    if (shmid == -1) {
        log_message("Failed to create shared memory\n");
        cleanup();
        exit(1);
    }

    shm_stats = (shm_stats_t*)shmat(shmid, NULL, 0);
    if (shm_stats == (shm_stats_t*)-1) {
        log_message("Failed to attach shared memory\n");
        cleanup();
        exit(1);
    }
    memset(shm_stats, 0, sizeof(shm_stats_t));
    log_message("Shared memory initialized with id: %d\n", shmid);

    msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msqid == -1) {
        log_message("Failed to create message queue\n");
        cleanup();
        exit(1);
    }
    log_message("Message queue created with id: %d\n", msqid);

    queue.buffer = malloc(cfg.TRIAGE_QUEUE_MAX * sizeof(patient_t));
    queue.capacity = cfg.TRIAGE_QUEUE_MAX;
    queue.count = 0;
    queue.head = 0;
    queue.tail = 0;
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.not_empty, NULL);

    triage_threads = malloc(cfg.TRIAGE_THREADS * sizeof(pthread_t));
    if (!triage_threads) {
        log_message("Failed to allocate memory for triage threads\n");
        cleanup();
        exit(1);
    }

    for (int i = 0; i < cfg.TRIAGE_THREADS; i++) {
        if (pthread_create(&triage_threads[i], NULL, triage_worker, NULL) != 0) {
            log_message("Failed to create triage thread %d\n", i);
            cleanup();
            exit(1);
        }
        log_message("Triage thread %d created\n", i + 1);
    }

    doctor_pids = malloc(cfg.DOCTORS * sizeof(pid_t));
    doctor_start_times = malloc(cfg.DOCTORS * sizeof(time_t));

    for (int i = 0; i < cfg.DOCTORS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            doctor_process(i);
            exit(0);
        } else if (pid > 0) {
            doctor_pids[i] = pid;
            doctor_start_times[i] = time(NULL);
        } else {
            log_message("Failed to fork doctor %d\n", i);
            cleanup();
            exit(1);
        }
    }

    log_message("All %d doctor processes spawned.\n", cfg.DOCTORS);

    // Set up signal handlers
    struct sigaction sigact;
    sigact.sa_handler = handler_sigint;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGINT, &sigact, NULL);

    sigact.sa_handler = handler_sigusr1;
    sigaction(SIGUSR1, &sigact, NULL);

    log_message("SIGINT and SIGUSR1 signals set up.\n");

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    log_message("SIGINT and SIGUSR1 signals unblocked.\n");

    log_message("Admission process started. Waiting for patients on '%s'\n", PIPE_NAME);

    fd_set read_fds;
    for (;;) {
        FD_ZERO(&read_fds);
        FD_SET(input_pipe, &read_fds);

        int ret = select(input_pipe + 1, &read_fds, NULL, NULL, NULL);
        if (ret == -1) {
            log_message("Failed to read from pipe\n");
            continue;
        }

        if (FD_ISSET(input_pipe, &read_fds)) {
            patient_t patient;
            ssize_t bytes = read(input_pipe, &patient, sizeof(patient_t));
            if (bytes > 0) {
                pthread_mutex_lock(&queue.mutex);
                if (queue.count == queue.capacity) {
                    log_message("Error: queue is full -> Dropping patient '%s'\n", patient.name);
                } else {
                    queue.buffer[queue.tail] = patient;
                    queue.tail = (queue.tail + 1) % queue.capacity;
                    queue.count++;
                    pthread_cond_signal(&queue.not_empty);
                }
                pthread_mutex_unlock(&queue.mutex);
            }
        }

        for (int i = 0; i < cfg.DOCTORS; i++) {
            if (time(NULL) - doctor_start_times[i] >= 5) {
                mq_message_t msg;
                msg.msgtype = i;
                if (msgsnd(msqid, &msg, sizeof(patient_t), 0) == -1) {
                    log_message("Failed to send termination message to doctor %d\n", i);
                } else {
                    log_message("Sent termination message to doctor %d\n", i);
                }

                doctor_start_times[i] = time(NULL);
                pid_t pid = fork();
                if (pid == 0) {
                    log_message("Started new doctor process %d\n", i);
                    doctor_process(i);
                    exit(0);
                } else if (pid > 0) {
                    doctor_pids[i] = pid;
                } else {
                    log_message("Failed to fork new doctor %d\n", i);
                    exit(1);
                }
            }
        }
    }
    return 0;
}

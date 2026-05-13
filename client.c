#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "distributed.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct
{
    const double* x;
    const double* y;
    uint64_t begin_interval;
    uint64_t end_interval;
    double partial;
} thread_task_t;

static void usage(const char* program)
{
    fprintf(stderr,
        "Usage: %s <host> <port> <cores> <worker-id> <connect-timeout-ms> <compute-timeout-ms>\n",
        program);
}

static void* compute_thread(void* arg)
{
    thread_task_t* task = arg;
    double partial = 0.0;

    for (uint64_t i = task->begin_interval; i < task->end_interval; ++i)
    {
        partial += task->y[i] * (task->x[i + 1] - task->x[i]);
    }

    task->partial = partial;
    return NULL;
}

static double compute_partial_integral(const double* x, const double* y, uint64_t intervals_count, int cores)
{
    int threads_count = cores;
    if ((uint64_t)threads_count > intervals_count)
    {
        threads_count = (int)intervals_count;
    }
    if (threads_count < 1)
    {
        threads_count = 1;
    }

    pthread_t* threads = calloc((size_t)threads_count, sizeof(pthread_t));
    thread_task_t* tasks = calloc((size_t)threads_count, sizeof(thread_task_t));
    if (threads == NULL || tasks == NULL)
    {
        perror("calloc");
        free(threads);
        free(tasks);
        exit(EXIT_FAILURE);
    }

    uint64_t cursor = 0;
    for (int i = 0; i < threads_count; ++i)
    {
        uint64_t remaining_intervals = intervals_count - cursor;
        uint64_t remaining_threads = (uint64_t)(threads_count - i);
        uint64_t share = remaining_intervals / remaining_threads;

        tasks[i].x = x;
        tasks[i].y = y;
        tasks[i].begin_interval = cursor;
        tasks[i].end_interval = cursor + share;
        tasks[i].partial = 0.0;
        cursor += share;

        if (pthread_create(&threads[i], NULL, compute_thread, &tasks[i]) != 0)
        {
            perror("pthread_create");
            free(threads);
            free(tasks);
            exit(EXIT_FAILURE);
        }
    }

    double total = 0.0;
    for (int i = 0; i < threads_count; ++i)
    {
        if (pthread_join(threads[i], NULL) != 0)
        {
            perror("pthread_join");
            free(threads);
            free(tasks);
            exit(EXIT_FAILURE);
        }
        total += tasks[i].partial;
    }

    free(threads);
    free(tasks);
    return total;
}

static int should_fail_after_task(int worker_id)
{
    const char* fail_worker = getenv("INTEGRAL_FAIL_WORKER_ID");
    if (fail_worker == NULL)
    {
        return 0;
    }

    return parse_int_arg(fail_worker, "INTEGRAL_FAIL_WORKER_ID", 0) == worker_id;
}

static int connect_with_retry(const char* host, const char* port, int timeout_ms)
{
    double deadline = timeout_ms == 0 ? 0.0 : monotonic_seconds() + (double)timeout_ms / 1000.0;

    for (;;)
    {
        int sock_fd = connect_to_server(host, port);
        if (sock_fd != -1)
        {
            return sock_fd;
        }

        if (timeout_ms != 0 && monotonic_seconds() >= deadline)
        {
            fprintf(stderr, "client: connection timeout while waiting for server %s:%s\n", host, port);
            return -1;
        }

        printf("client[%ld]: wait for server %s:%s\n", (long)getpid(), host, port);
        sleep_milliseconds(50);
    }
}

int distributed_client_main(int argc, char** argv)
{
    if (argc != 7)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* host = argv[1];
    const char* port = argv[2];
    int cores = parse_int_arg(argv[3], "cores", 0);
    int worker_id = parse_int_arg(argv[4], "worker-id", 0);
    int connect_timeout_ms = parse_int_arg(argv[5], "connect-timeout-ms", 1);
    int compute_timeout_ms = parse_int_arg(argv[6], "compute-timeout-ms", 1);
    if (cores == 0)
    {
        cores = (int)detect_cpu_count();
    }

    int sock_fd = connect_with_retry(host, port, connect_timeout_ms);
    if (sock_fd == -1)
    {
        return EXIT_FAILURE;
    }

    hello_msg_t hello =
    {
        .type = MSG_HELLO,
        .cores = (uint32_t)cores,
        .worker_id = worker_id < 0 ? UINT32_MAX : (uint32_t)worker_id
    };
    if (write_full(sock_fd, &hello, sizeof(hello)) != 1)
    {
        perror("send hello");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("client[%ld]: connected, worker_id=%u, cores=%u\n",
        (long)getpid(),
        hello.worker_id,
        hello.cores);

    task_header_t task;
    int read_status = read_full(sock_fd, &task, sizeof(task));
    if (read_status != 1)
    {
        fprintf(stderr, "client: unable to read task header\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (task.type == MSG_STOP)
    {
        close(sock_fd);
        return EXIT_SUCCESS;
    }

    if (task.type != MSG_TASK || task.points_count < 2)
    {
        fprintf(stderr, "client: invalid task received\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    double* x = calloc((size_t)task.points_count, sizeof(double));
    double* y = calloc((size_t)task.points_count, sizeof(double));
    if (x == NULL || y == NULL)
    {
        perror("calloc");
        free(x);
        free(y);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (read_full(sock_fd, x, (size_t)task.points_count * sizeof(double)) != 1 ||
        read_full(sock_fd, y, (size_t)task.points_count * sizeof(double)) != 1)
    {
        fprintf(stderr, "client: unable to read task arrays\n");
        free(x);
        free(y);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    if (should_fail_after_task(worker_id))
    {
        fprintf(stderr, "client: injected failure after receiving task, worker_id=%d\n", worker_id);
        free(x);
        free(y);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    uint64_t intervals_count = task.points_count - 1;
    double compute_start_time = monotonic_seconds();
    double partial = compute_partial_integral(x, y, intervals_count, cores);
    double compute_elapsed_time = monotonic_seconds() - compute_start_time;
    if (compute_timeout_ms != 0 && compute_elapsed_time * 1000.0 > (double)compute_timeout_ms)
    {
        fprintf(stderr,
            "client: compute timeout, worker_id=%d, elapsed=%.6f sec, limit=%d ms\n",
            worker_id,
            compute_elapsed_time,
            compute_timeout_ms);
        free(x);
        free(y);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    result_msg_t result =
    {
        .type = MSG_RESULT,
        .task_id = task.task_id,
        .partial_integral = partial
    };
    if (write_full(sock_fd, &result, sizeof(result)) != 1)
    {
        perror("send result");
        free(x);
        free(y);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    task_header_t completion;
    if (read_full(sock_fd, &completion, sizeof(completion)) != 1 || completion.type != MSG_STOP)
    {
        fprintf(stderr, "client: unable to read completion ack from server\n");
        free(x);
        free(y);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("client[%ld]: task %u [%llu, %llu), cores=%d, intervals=%llu, compute_time=%.6f sec, partial=%.12f\n",
        (long)getpid(),
        task.task_id,
        (unsigned long long)task.begin_idx,
        (unsigned long long)task.end_idx,
        cores,
        (unsigned long long)intervals_count,
        compute_elapsed_time,
        partial);

    free(x);
    free(y);
    close(sock_fd);
    return EXIT_SUCCESS;
}

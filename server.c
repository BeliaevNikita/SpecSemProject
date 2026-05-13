#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "distributed.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct
{
    int fd;
    int connected;
    int has_result;
    uint32_t cores;
    uint64_t begin_idx;
    uint64_t end_idx;
} worker_t;

static int first_free_worker_slot(const worker_t* workers, int num_clients)
{
    for (int i = 0; i < num_clients; ++i)
    {
        if (!workers[i].connected)
        {
            return i;
        }
    }

    return -1;
}

static void usage(const char* program)
{
    fprintf(stderr, "Usage: %s <points-file> <num-clients> <port> <server-timeout-ms>\n", program);
}

static int remaining_timeout_ms(double deadline)
{
    if (deadline == 0.0)
    {
        return 0;
    }

    double remaining = deadline - monotonic_seconds();
    if (remaining <= 0.0)
    {
        return -1;
    }

    int remaining_ms = (int)(remaining * 1000.0);
    return remaining_ms < 1 ? 1 : remaining_ms;
}

static int accept_workers(int listen_fd, worker_t* workers, int num_clients, double deadline)
{
    for (int i = 0; i < num_clients; ++i)
    {
        int timeout_ms = remaining_timeout_ms(deadline);
        if (timeout_ms < 0)
        {
            fprintf(stderr, "server: timeout while waiting for workers\n");
            return -1;
        }

        int ready = wait_readable(listen_fd, timeout_ms);
        if (ready == 0)
        {
            fprintf(stderr, "server: timeout while waiting for workers\n");
            return -1;
        }
        if (ready < 0)
        {
            perror("poll accept");
            return -1;
        }

        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd == -1)
        {
            perror("accept");
            return -1;
        }

        if (enable_tcp_keepalive(client_fd) == -1)
        {
            perror("setsockopt SO_KEEPALIVE");
        }

        hello_msg_t hello;
        timeout_ms = remaining_timeout_ms(deadline);
        if (timeout_ms < 0 ||
            read_full_timeout(client_fd, &hello, sizeof(hello), timeout_ms) != 1 ||
            hello.type != MSG_HELLO ||
            hello.cores == 0)
        {
            fprintf(stderr, "server: invalid hello from worker\n");
            close(client_fd);
            return -1;
        }

        int slot = -1;
        if (hello.worker_id < (uint32_t)num_clients && !workers[hello.worker_id].connected)
        {
            slot = (int)hello.worker_id;
        }
        else
        {
            slot = first_free_worker_slot(workers, num_clients);
        }

        if (slot == -1)
        {
            fprintf(stderr, "server: no free worker slots\n");
            close(client_fd);
            return -1;
        }

        workers[slot].fd = client_fd;
        workers[slot].connected = 1;
        workers[slot].has_result = 0;
        workers[slot].cores = hello.cores;
        workers[slot].begin_idx = 0;
        workers[slot].end_idx = 0;

        printf("server: worker %d connected, cores=%u\n", slot, hello.cores);
    }

    return 0;
}

static void split_work(worker_t* workers, int num_clients, uint64_t intervals_count)
{
    uint64_t total_cores = 0;
    for (int i = 0; i < num_clients; ++i)
    {
        total_cores += workers[i].cores;
    }

    uint64_t cursor = 0;
    uint64_t assigned = 0;
    for (int i = 0; i < num_clients; ++i)
    {
        uint64_t share = 0;
        if (i == num_clients - 1)
        {
            share = intervals_count - assigned;
        }
        else
        {
            share = (intervals_count * workers[i].cores) / total_cores;
            if (share == 0 && assigned < intervals_count)
            {
                share = 1;
            }
        }

        if (assigned + share > intervals_count)
        {
            share = intervals_count - assigned;
        }

        workers[i].begin_idx = cursor;
        workers[i].end_idx = cursor + share;
        cursor += share;
        assigned += share;
    }
}

static void send_tasks(const worker_t* workers, int num_clients, const double* x, const double* y)
{
    for (int i = 0; i < num_clients; ++i)
    {
        uint64_t points_count = workers[i].end_idx - workers[i].begin_idx + 1;
        task_header_t task =
        {
            .type = MSG_TASK,
            .task_id = (uint32_t)i,
            .begin_idx = workers[i].begin_idx,
            .end_idx = workers[i].end_idx,
            .points_count = points_count
        };

        if (points_count < 2)
        {
            task.type = MSG_STOP;
            task.points_count = 0;
            if (write_full(workers[i].fd, &task, sizeof(task)) != 1)
            {
                perror("send stop");
                exit(EXIT_FAILURE);
            }
            continue;
        }

        size_t begin = (size_t)workers[i].begin_idx;
        size_t bytes = (size_t)points_count * sizeof(double);
        if (write_full(workers[i].fd, &task, sizeof(task)) != 1 ||
            write_full(workers[i].fd, x + begin, bytes) != 1 ||
            write_full(workers[i].fd, y + begin, bytes) != 1)
        {
            perror("send task");
            exit(EXIT_FAILURE);
        }

        printf("server: sent task %d [%llu, %llu)\n",
            i,
            (unsigned long long)workers[i].begin_idx,
            (unsigned long long)workers[i].end_idx);
    }
}

static int collect_results(worker_t* workers, int num_clients, double* total_out, double deadline)
{
    double total = 0.0;
    int failed = 0;
    for (int i = 0; i < num_clients; ++i)
    {
        if (workers[i].end_idx == workers[i].begin_idx)
        {
            workers[i].has_result = 1;
            continue;
        }

        result_msg_t result;
        int timeout_ms = remaining_timeout_ms(deadline);
        if (timeout_ms < 0)
        {
            fprintf(stderr, "server: timeout while waiting for worker %d result\n", i);
            failed = -1;
            continue;
        }

        if (read_full_timeout(workers[i].fd, &result, sizeof(result), timeout_ms) != 1 ||
            result.type != MSG_RESULT ||
            result.task_id != (uint32_t)i)
        {
            fprintf(stderr, "server: invalid result from worker %d\n", i);
            failed = -1;
            continue;
        }

        printf("server: result task %u = %.12f\n", result.task_id, result.partial_integral);
        total += result.partial_integral;
        workers[i].has_result = 1;
    }

    *total_out = total;
    return failed;
}

static void send_completion_acks(const worker_t* workers, int num_clients)
{
    task_header_t completion =
    {
        .type = MSG_STOP,
        .task_id = 0,
        .begin_idx = 0,
        .end_idx = 0,
        .points_count = 0
    };

    for (int i = 0; i < num_clients; ++i)
    {
        if (!workers[i].has_result)
        {
            continue;
        }

        if (write_full(workers[i].fd, &completion, sizeof(completion)) != 1)
        {
            perror("send completion ack");
        }
    }
}

static int should_fail_after_tasks(void)
{
    const char* fail_server = getenv("INTEGRAL_FAIL_SERVER_AFTER_TASKS");
    return fail_server != NULL && strcmp(fail_server, "0") != 0;
}

int distributed_server_main(int argc, char** argv)
{
    if (argc != 5)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* points_file = argv[1];
    int num_clients = parse_int_arg(argv[2], "num-clients", 1);
    const char* port = argv[3];
    int server_timeout_ms = parse_int_arg(argv[4], "server-timeout-ms", 1);

    table_points_t table = read_table_points(points_file);

    int listen_fd = create_listen_socket(port);
    double start_time = monotonic_seconds();
    double deadline = server_timeout_ms == 0 ? 0.0 : start_time + (double)server_timeout_ms / 1000.0;
    printf("server: listening on port %s, need %d workers\n", port, num_clients);

    worker_t* workers = calloc((size_t)num_clients, sizeof(worker_t));
    if (workers == NULL)
    {
        perror("calloc");
        close(listen_fd);
        free_table_points(&table);
        return EXIT_FAILURE;
    }

    if (accept_workers(listen_fd, workers, num_clients, deadline) != 0)
    {
        close(listen_fd);
        for (int i = 0; i < num_clients; ++i)
        {
            if (workers[i].connected)
            {
                close(workers[i].fd);
            }
        }
        free(workers);
        free_table_points(&table);
        return EXIT_FAILURE;
    }
    close(listen_fd);

    split_work(workers, num_clients, (uint64_t)table.count - 1);
    send_tasks(workers, num_clients, table.x, table.y);
    if (should_fail_after_tasks())
    {
        fprintf(stderr, "server: injected failure after sending tasks\n");
        for (int i = 0; i < num_clients; ++i)
        {
            close(workers[i].fd);
        }
        free(workers);
        free_table_points(&table);
        return EXIT_FAILURE;
    }

    double total = 0.0;
    int failed = collect_results(workers, num_clients, &total, deadline);
    send_completion_acks(workers, num_clients);

    for (int i = 0; i < num_clients; ++i)
    {
        close(workers[i].fd);
    }

    printf("Integral = %.12f\n", total);
    printf("server: elapsed time = %.6f sec\n", monotonic_seconds() - start_time);

    free(workers);
    free_table_points(&table);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

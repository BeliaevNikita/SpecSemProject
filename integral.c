#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "distributed.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_CONNECT_TIMEOUT_MS "120000"
#define DEFAULT_COMPUTE_TIMEOUT_MS "120000"
#define DEFAULT_SERVER_TIMEOUT_MS "120000"

static void usage(const char* program)
{
    fprintf(stderr,
        "Usage: %s <func-id> <x-begin> <x-end> <num-points> <num-clients> [port [client-cores...]]\n"
        "func-id: 0=sin(x), 1=cos(x), 2=x*x\n",
        program);
}

static double eval_func(int func_id, double x)
{
    switch (func_id)
    {
        case 0:
            return sin(x);
        case 1:
            return cos(x);
        case 2:
            return x * x;
        default:
            fprintf(stderr, "Unknown func-id: %d\n", func_id);
            exit(EXIT_FAILURE);
    }
}

static void write_points_file(const char* filename, int func_id, double begin, double end, int num_points)
{
    FILE* file = fopen(filename, "w");
    if (file == NULL)
    {
        fprintf(stderr, "Unable to create '%s': %s\n", filename, strerror(errno));
        exit(EXIT_FAILURE);
    }

    double step = (end - begin) / (double)(num_points - 1);
    for (int i = 0; i < num_points; ++i)
    {
        double x = begin + step * (double)i;
        fprintf(file, "%.17g %.17g\n", x, eval_func(func_id, x));
    }

    if (fclose(file) != 0)
    {
        fprintf(stderr, "Unable to close '%s': %s\n", filename, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

typedef int (*module_main_fn)(int argc, char** argv);

static int count_argv(char** argv)
{
    int argc = 0;
    while (argv[argc] != NULL)
    {
        ++argc;
    }

    return argc;
}

static pid_t spawn_module(module_main_fn module_main, char** argv)
{
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        int status = module_main(count_argv(argv), argv);
        fflush(NULL);
        _exit(status);
    }

    return pid;
}

static int wait_for_child(pid_t pid, const char* name)
{
    int status = 0;
    if (waitpid(pid, &status, 0) == -1)
    {
        perror("waitpid");
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        fprintf(stderr, "%s failed\n", name);
        return -1;
    }

    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int func_id = parse_int_arg(argv[1], "func-id", 0);
    if (func_id > 2)
    {
        fprintf(stderr, "func-id must be 0, 1 or 2\n");
        return EXIT_FAILURE;
    }

    double x_begin = parse_double_arg(argv[2], "x-begin");
    double x_end = parse_double_arg(argv[3], "x-end");
    int num_points = parse_int_arg(argv[4], "num-points", 2);
    int num_clients = parse_int_arg(argv[5], "num-clients", 1);
    const char* port = argc >= 7 ? argv[6] : DEFAULT_PORT;
    char** client_cores = NULL;

    if (argc > 7)
    {
        if (argc != 7 + num_clients)
        {
            fprintf(stderr, "Expected exactly %d client core values after port\n", num_clients);
            usage(argv[0]);
            return EXIT_FAILURE;
        }

        client_cores = &argv[7];
        for (int i = 0; i < num_clients; ++i)
        {
            parse_int_arg(client_cores[i], "client-cores", 1);
        }
    }

    const char* points_file = "build/points.txt";
    write_points_file(points_file, func_id, x_begin, x_end, num_points);

    pid_t* clients = calloc((size_t)num_clients, sizeof(pid_t));
    char (*worker_id_args)[32] = calloc((size_t)num_clients, sizeof(*worker_id_args));
    if (clients == NULL)
    {
        perror("calloc");
        free(worker_id_args);
        return EXIT_FAILURE;
    }
    if (worker_id_args == NULL)
    {
        perror("calloc");
        free(clients);
        return EXIT_FAILURE;
    }

    printf("integral: generated %s\n", points_file);
    printf("integral: starting %d clients before server\n", num_clients);
    if (client_cores != NULL)
    {
        printf("integral: client cores:");
        for (int i = 0; i < num_clients; ++i)
        {
            printf(" %s", client_cores[i]);
        }
        printf("\n");
    }
    fflush(stdout);

    double start_time = monotonic_seconds();
    for (int i = 0; i < num_clients; ++i)
    {
        snprintf(worker_id_args[i], sizeof(worker_id_args[i]), "%d", i);

        char* client_argv[] =
        {
            "client",
            (char*)DEFAULT_HOST,
            (char*)port,
            client_cores == NULL ? "0" : client_cores[i],
            worker_id_args[i],
            DEFAULT_CONNECT_TIMEOUT_MS,
            DEFAULT_COMPUTE_TIMEOUT_MS,
            NULL
        };
        clients[i] = spawn_module(distributed_client_main, client_argv);
    }

    sleep_milliseconds(100);

    char num_clients_arg[32];
    snprintf(num_clients_arg, sizeof(num_clients_arg), "%d", num_clients);
    char* server_argv[] =
    {
        "server",
        (char*)points_file,
        num_clients_arg,
        (char*)port,
        DEFAULT_SERVER_TIMEOUT_MS,
        NULL
    };
    pid_t server_pid = spawn_module(distributed_server_main, server_argv);

    int failed = wait_for_child(server_pid, "server");
    if (failed != 0)
    {
        for (int i = 0; i < num_clients; ++i)
        {
            kill(clients[i], SIGTERM);
        }
    }

    for (int i = 0; i < num_clients; ++i)
    {
        if (wait_for_child(clients[i], "client") != 0)
        {
            failed = -1;
        }
    }

    printf("integral: elapsed time = %.6f sec\n", monotonic_seconds() - start_time);

    free(worker_id_args);
    free(clients);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

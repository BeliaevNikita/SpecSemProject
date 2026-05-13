#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "distributed.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(const char* program)
{
    fprintf(stderr, "Usage: %s <host> <port> <connect-timeout-ms> <compute-timeout-ms> <cores...>\n", program);
}

static int wait_for_worker(pid_t pid)
{
    int status = 0;
    if (waitpid(pid, &status, 0) == -1)
    {
        perror("waitpid");
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
    {
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

    const char* host = argv[1];
    const char* port = argv[2];
    const char* connect_timeout_ms = argv[3];
    const char* compute_timeout_ms = argv[4];
    parse_int_arg(connect_timeout_ms, "connect-timeout-ms", 1);
    parse_int_arg(compute_timeout_ms, "compute-timeout-ms", 1);
    int workers_count = argc - 5;

    pid_t* workers = calloc((size_t)workers_count, sizeof(pid_t));
    if (workers == NULL)
    {
        perror("calloc");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < workers_count; ++i)
    {
        parse_int_arg(argv[5 + i], "cores", 1);

        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork");
            for (int j = 0; j < i; ++j)
            {
                kill(workers[j], SIGTERM);
            }
            free(workers);
            return EXIT_FAILURE;
        }

        if (pid == 0)
        {
            char worker_id[32];
            snprintf(worker_id, sizeof(worker_id), "%d", i);

            char* client_argv[] =
            {
                "library-worker",
                (char*)host,
                (char*)port,
                argv[5 + i],
                worker_id,
                (char*)connect_timeout_ms,
                (char*)compute_timeout_ms,
                NULL
            };

            int status = distributed_client_main(7, client_argv);
            fflush(NULL);
            _exit(status);
        }

        workers[i] = pid;
    }

    int failed = 0;
    for (int i = 0; i < workers_count; ++i)
    {
        if (wait_for_worker(workers[i]) != 0)
        {
            fprintf(stderr, "library_workers: worker %d failed\n", i);
            failed = -1;
        }
    }

    free(workers);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "distributed.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SERVER_TIMEOUT_MS "120000"

static void usage(const char* program)
{
    fprintf(stderr,
        "Usage: %s <func-id> <x-begin> <x-end> <num-points> <num-clients> [port]\n"
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

int main(int argc, char** argv)
{
    if (argc < 6 || argc > 7)
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

    const char* points_file = "build/points.txt";
    write_points_file(points_file, func_id, x_begin, x_end, num_points);

    printf("integral2: generated %s\n", points_file);
    printf("integral2: workers must already be running, starting server\n");
    fflush(stdout);

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

    return distributed_server_main(5, server_argv);
}

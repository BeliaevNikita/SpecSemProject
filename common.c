#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int parse_int_arg(const char* text, const char* name, int min_value)
{
    char* end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || *end != '\0' || value < min_value || value > INT_MAX)
    {
        fprintf(stderr, "Unable to parse %s: '%s'\n", name, text);
        exit(EXIT_FAILURE);
    }

    return (int)value;
}

double parse_double_arg(const char* text, const char* name)
{
    char* end = NULL;
    errno = 0;
    double value = strtod(text, &end);
    if (errno != 0 || text[0] == '\0' || *end != '\0')
    {
        fprintf(stderr, "Unable to parse %s: '%s'\n", name, text);
        exit(EXIT_FAILURE);
    }

    return value;
}

int read_full(int fd, void* buffer, size_t size)
{
    char* cursor = buffer;
    size_t done = 0;

    while (done < size)
    {
        ssize_t nread = read(fd, cursor + done, size - done);
        if (nread == 0)
        {
            return 0;
        }
        if (nread < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        done += (size_t)nread;
    }

    return 1;
}

int wait_readable(int fd, int timeout_ms)
{
    struct pollfd pfd =
    {
        .fd = fd,
        .events = POLLIN,
        .revents = 0
    };

    for (;;)
    {
        int ready = poll(&pfd, 1, timeout_ms == 0 ? -1 : timeout_ms);
        if (ready > 0)
        {
            return 1;
        }
        if (ready == 0)
        {
            return 0;
        }
        if (errno != EINTR)
        {
            return -1;
        }
    }
}

int read_full_timeout(int fd, void* buffer, size_t size, int timeout_ms)
{
    char* cursor = buffer;
    size_t done = 0;
    double deadline = timeout_ms == 0 ? 0.0 : monotonic_seconds() + (double)timeout_ms / 1000.0;

    while (done < size)
    {
        int remaining_ms = 0;
        if (timeout_ms != 0)
        {
            double remaining = deadline - monotonic_seconds();
            if (remaining <= 0.0)
            {
                return 0;
            }
            remaining_ms = (int)(remaining * 1000.0);
            if (remaining_ms < 1)
            {
                remaining_ms = 1;
            }
        }

        int ready = wait_readable(fd, remaining_ms);
        if (ready <= 0)
        {
            return ready;
        }

        ssize_t nread = read(fd, cursor + done, size - done);
        if (nread == 0)
        {
            return 0;
        }
        if (nread < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        done += (size_t)nread;
    }

    return 1;
}

int write_full(int fd, const void* buffer, size_t size)
{
    const char* cursor = buffer;
    size_t done = 0;

    while (done < size)
    {
        ssize_t nwritten = write(fd, cursor + done, size - done);
        if (nwritten < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        done += (size_t)nwritten;
    }

    return 1;
}

int enable_tcp_keepalive(int fd)
{
    int yes = 1;
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
}

int create_listen_socket(const char* port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* result = NULL;
    int err = getaddrinfo(NULL, port, &hints, &result);
    if (err != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    int listen_fd = -1;
    for (struct addrinfo* it = result; it != NULL; it = it->ai_next)
    {
        listen_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (listen_fd == -1)
        {
            continue;
        }

        int yes = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
        {
            close(listen_fd);
            listen_fd = -1;
            continue;
        }

        if (bind(listen_fd, it->ai_addr, it->ai_addrlen) == 0)
        {
            break;
        }

        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(result);

    if (listen_fd == -1)
    {
        perror("Unable to bind listen socket");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, LISTEN_BACKLOG) == -1)
    {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    return listen_fd;
}

int connect_to_server(const char* host, const char* port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = NULL;
    int err = getaddrinfo(host, port, &hints, &result);
    if (err != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    int sock_fd = -1;
    for (struct addrinfo* it = result; it != NULL; it = it->ai_next)
    {
        sock_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock_fd == -1)
        {
            continue;
        }

        if (connect(sock_fd, it->ai_addr, it->ai_addrlen) == 0)
        {
            if (enable_tcp_keepalive(sock_fd) == -1)
            {
                perror("setsockopt SO_KEEPALIVE");
            }
            break;
        }

        close(sock_fd);
        sock_fd = -1;
    }

    freeaddrinfo(result);
    return sock_fd;
}

table_points_t read_table_points(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Unable to open points file '%s': %s\n", filename, strerror(errno));
        exit(EXIT_FAILURE);
    }

    table_points_t table =
    {
        .count = 0,
        .x = NULL,
        .y = NULL
    };

    size_t capacity = 128;
    table.x = malloc(capacity * sizeof(double));
    table.y = malloc(capacity * sizeof(double));
    if (table.x == NULL || table.y == NULL)
    {
        perror("malloc");
        fclose(file);
        free_table_points(&table);
        exit(EXIT_FAILURE);
    }

    while (fscanf(file, "%lf %lf", &table.x[table.count], &table.y[table.count]) == 2)
    {
        ++table.count;
        if (table.count == capacity)
        {
            capacity *= 2;
            double* new_x = realloc(table.x, capacity * sizeof(double));
            double* new_y = realloc(table.y, capacity * sizeof(double));
            if (new_x == NULL || new_y == NULL)
            {
                perror("realloc");
                fclose(file);
                free(new_x == NULL ? table.x : new_x);
                free(new_y == NULL ? table.y : new_y);
                exit(EXIT_FAILURE);
            }
            table.x = new_x;
            table.y = new_y;
        }
    }

    if (ferror(file))
    {
        fprintf(stderr, "Unable to read points file '%s'\n", filename);
        fclose(file);
        free_table_points(&table);
        exit(EXIT_FAILURE);
    }

    fclose(file);

    if (table.count < 2)
    {
        fprintf(stderr, "Points file must contain at least two points\n");
        free_table_points(&table);
        exit(EXIT_FAILURE);
    }

    return table;
}

void free_table_points(table_points_t* table)
{
    if (table == NULL)
    {
        return;
    }

    free(table->x);
    free(table->y);
    table->count = 0;
    table->x = NULL;
    table->y = NULL;
}

long detect_cpu_count(void)
{
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1)
    {
        return 1;
    }

    return cores;
}

double monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

void sleep_milliseconds(long milliseconds)
{
    struct timespec timeout =
    {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L
    };

    while (nanosleep(&timeout, &timeout) == -1)
    {
        if (errno != EINTR)
        {
            perror("nanosleep");
            exit(EXIT_FAILURE);
        }
    }
}

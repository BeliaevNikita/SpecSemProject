#ifndef DISTRIBUTED_INTEGRAL_COMMON_H
#define DISTRIBUTED_INTEGRAL_COMMON_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file common.h
 * @brief Публичный API протокола, таблиц и сетевых функций библиотеки распределённого интеграла.
 */

/**
 * @brief Адрес loopback по умолчанию для локальных клиентов.
 */
#define DEFAULT_HOST "127.0.0.1"

/**
 * @brief TCP-порт по умолчанию для примеров запуска.
 */
#define DEFAULT_PORT "1337"

/**
 * @brief Максимальное число ожидающих TCP-подключений для серверного сокета.
 */
#define LISTEN_BACKLOG 16

/**
 * @brief Идентификаторы сообщений бинарного протокола.
 *
 * Тип сообщения хранится первым полем в каждой структуре протокола.
 *
 * @warning Протокол отправляет нативные структуры C и значения double без
 * преобразования порядка байт, поэтому в этом учебном проекте он рассчитан на
 * однородные хосты.
 */
typedef enum
{
    /** @brief Клиент сообщает о себе и передаёт число ядер. */
    MSG_HELLO = 1,
    /** @brief Сервер отправляет одну задачу интегрирования и срез таблицы. */
    MSG_TASK = 2,
    /** @brief Клиент возвращает частичный интеграл для задачи. */
    MSG_RESULT = 3,
    /** @brief Сервер просит клиента завершиться или подтверждает успешное выполнение. */
    MSG_STOP = 4
} message_type_t;

/**
 * @brief Приветственное сообщение от клиента серверу.
 */
typedef struct
{
    /** @brief Тип сообщения, должен быть MSG_HELLO. */
    uint32_t type;
    /** @brief Число CPU-ядер worker-а, используемое сервером как вес при разбиении работы. */
    uint32_t cores;
    /** @brief Необязательный стабильный id worker-а или UINT32_MAX, если слот выбирает сервер. */
    uint32_t worker_id;
} hello_msg_t;

/**
 * @brief Заголовок задачи от сервера клиенту.
 *
 * После заголовка передаются points_count значений double для x[], а затем
 * points_count значений double для y[].
 */
typedef struct
{
    /** @brief Тип сообщения, обычно MSG_TASK или MSG_STOP. */
    uint32_t type;
    /** @brief Идентификатор задачи, который worker возвращает в result_msg_t. */
    uint32_t task_id;
    /** @brief Первый глобальный индекс прямоугольника, входящий в задачу. */
    uint64_t begin_idx;
    /** @brief Индекс за последним глобальным прямоугольником задачи. */
    uint64_t end_idx;
    /** @brief Число точек x/y, отправленных после этого заголовка. */
    uint64_t points_count;
} task_header_t;

/**
 * @brief Сообщение с результатом задачи от клиента серверу.
 */
typedef struct
{
    /** @brief Тип сообщения, должен быть MSG_RESULT. */
    uint32_t type;
    /** @brief Идентификатор завершённой задачи. */
    uint32_t task_id;
    /** @brief Частичный интеграл методом прямоугольников, вычисленный worker-ом. */
    double partial_integral;
} result_msg_t;

/**
 * @brief Таблица точек функции x/y в памяти.
 */
typedef struct
{
    /** @brief Число действительных элементов в массивах x и y. */
    size_t count;
    /** @brief Координаты x, прочитанные из файла таблицы. */
    double* x;
    /** @brief Координаты y, прочитанные из файла таблицы. */
    double* y;
} table_points_t;

/**
 * @brief Разобрать целочисленный аргумент командной строки.
 * @param text Строка для разбора.
 * @param name Человекочитаемое имя аргумента для диагностических сообщений.
 * @param min_value Минимально допустимое значение.
 * @return Разобранное целочисленное значение.
 * @warning При некорректном вводе завершает процесс с EXIT_FAILURE.
 */
int parse_int_arg(const char* text, const char* name, int min_value);

/**
 * @brief Разобрать аргумент командной строки типа double.
 * @param text Строка для разбора.
 * @param name Человекочитаемое имя аргумента для диагностических сообщений.
 * @return Разобранное значение double.
 * @warning При некорректном вводе завершает процесс с EXIT_FAILURE.
 */
double parse_double_arg(const char* text, const char* name);

/**
 * @brief Прочитать ровно size байт из файлового дескриптора.
 * @param fd Файловый дескриптор для чтения.
 * @param buffer Буфер назначения.
 * @param size Число байт для чтения.
 * @return 1 при успехе, 0 при EOF до заполнения буфера, -1 при системной ошибке.
 */
int read_full(int fd, void* buffer, size_t size);

/**
 * @brief Прочитать ровно size байт с ограничением времени ожидания.
 * @param fd Файловый дескриптор для чтения.
 * @param buffer Буфер назначения.
 * @param size Число байт для чтения.
 * @param timeout_ms Максимальное время ожидания в миллисекундах, 0 означает без таймаута.
 * @return 1 при успехе, 0 при EOF или таймауте до заполнения буфера, -1 при системной ошибке.
 */
int read_full_timeout(int fd, void* buffer, size_t size, int timeout_ms);

/**
 * @brief Записать ровно size байт в файловый дескриптор.
 * @param fd Файловый дескриптор для записи.
 * @param buffer Исходный буфер.
 * @param size Число байт для записи.
 * @return 1 при успехе, -1 при системной ошибке.
 */
int write_full(int fd, const void* buffer, size_t size);

/**
 * @brief Включить TCP keepalive на подключённом сокете.
 * @param fd Файловый дескриптор TCP-сокета.
 * @return 0 при успехе, -1 при ошибке setsockopt().
 * @warning TCP keepalive является дополнительной страховкой. В проекте быстрые
 * отказы отслеживаются прикладными таймаутами чтения, подключения и ожидания.
 */
int enable_tcp_keepalive(int fd);

/**
 * @brief Дождаться готовности файлового дескриптора к чтению.
 * @param fd Файловый дескриптор.
 * @param timeout_ms Максимальное время ожидания в миллисекундах, 0 означает без таймаута.
 * @return 1 если дескриптор готов, 0 при таймауте, -1 при ошибке poll().
 */
int wait_readable(int fd, int timeout_ms);

/**
 * @brief Создать TCP-сокет сервера, привязать его и начать прослушивание.
 * @param port TCP-порт в виде десятичной строки.
 * @return Файловый дескриптор слушающего сокета.
 * @warning Завершает процесс с EXIT_FAILURE, если сокет нельзя создать.
 */
int create_listen_socket(const char* port);

/**
 * @brief Подключиться к TCP-серверу.
 * @param host Имя хоста сервера или IPv4-адрес.
 * @param port TCP-порт в виде десятичной строки.
 * @return Файловый дескриптор подключённого сокета или -1, если все попытки подключения не удались.
 */
int connect_to_server(const char* host, const char* port);

/**
 * @brief Прочитать разделённую пробельными символами таблицу пар x y.
 * @param filename Путь к текстовому файлу, где в каждой строке хранится пара "x y".
 * @return Выделенная table_points_t. Освобождается через free_table_points().
 * @warning При ошибках ввода-вывода, выделения памяти или формата завершает процесс с EXIT_FAILURE.
 */
table_points_t read_table_points(const char* filename);

/**
 * @brief Освободить массивы, принадлежащие объекту table_points_t.
 * @param table Таблица, возвращённая read_table_points().
 */
void free_table_points(table_points_t* table);

/**
 * @brief Определить число доступных CPU-ядер.
 * @return Положительное число доступных ядер или 1, если определение не удалось.
 */
long detect_cpu_count(void);

/**
 * @brief Получить монотонное время в секундах.
 * @return Секунды от CLOCK_MONOTONIC в виде double.
 * @warning Завершает процесс с EXIT_FAILURE, если clock_gettime() завершилась ошибкой.
 */
double monotonic_seconds(void);

/**
 * @brief Уснуть минимум на заданное число миллисекунд.
 * @param milliseconds Длительность задержки в миллисекундах.
 * @warning Завершает процесс с EXIT_FAILURE, если nanosleep() завершилась ошибкой по причине, отличной от EINTR.
 */
void sleep_milliseconds(long milliseconds);

#endif

#ifndef DISTRIBUTED_INTEGRAL_DISTRIBUTED_H
#define DISTRIBUTED_INTEGRAL_DISTRIBUTED_H

/**
 * @file distributed.h
 * @brief Публичные точки входа модулей сервера и клиента.
 */

/**
 * @brief Запустить модуль рабочего клиента.
 * @param argc Число аргументов командной строки.
 * @param argv Аргументы в том же формате, что и у отдельного бинарника client:
 * argv[0] host port cores worker-id connect-timeout-ms compute-timeout-ms.
 * @return EXIT_SUCCESS при успехе, EXIT_FAILURE при некорректном вводе или сетевой ошибке.
 * @warning Функция может косвенно вызвать exit() через общие функции разбора
 * аргументов и сетевые helper-ы при обнаружении фатальных ошибок.
 */
int distributed_client_main(int argc, char** argv);

/**
 * @brief Запустить модуль управляющего сервера.
 * @param argc Число аргументов командной строки.
 * @param argv Аргументы в том же формате, что и у отдельного бинарника server:
 * argv[0] points-file num-clients port server-timeout-ms.
 * @return EXIT_SUCCESS при успехе, EXIT_FAILURE при некорректном вводе или отказе worker-а.
 * @warning Функция блокируется до подключения всех ожидаемых worker-ов, потому
 * что учебный протокол предполагает фиксированный набор worker-ов до раздачи задач.
 */
int distributed_server_main(int argc, char** argv);

#endif

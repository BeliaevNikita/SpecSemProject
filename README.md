# Распределённый табличный интеграл

Минимальный пример TCP-связки:

- `integral` генерирует файл точек функции и запускает несколько `client`, затем `server`;
- `client` ждёт сервер, сообщает число ядер, принимает одну задачу, делит её между POSIX threads, печатает время локального расчёта и возвращает частичную сумму;
- `server` принимает все подключения, делит интервалы между worker по числу ядер, рассылает точки и печатает итог;
- `server` и `integral` печатают wall-clock время работы.

`client.c` и `server.c` собираются в статическую библиотеку
`build/libdistributed_integral.a`. Отдельные программы `build/client` и
`build/server` являются тонкими wrapper-ами, а `integral`/`integral2`
линкуются с этой же библиотекой и вызывают серверный/клиентский код напрямую.

## Протокол

Все сообщения бинарные:

- `HELLO`: `type`, `cores`, `worker_id`;
- `TASK`: `type`, `task_id`, `begin_idx`, `end_idx`, `points_count`, затем `x[]`, затем `y[]`;
- `RESULT`: `type`, `task_id`, `partial_integral`;
- `STOP`: используется, если worker не достался непустой диапазон, а также как финальное подтверждение после успешного получения результата.

Диапазон `[begin_idx, end_idx)` относится к индексам прямоугольников. Для него передаётся `end_idx - begin_idx + 1` точек, потому что worker считает `y[i] * (x[i + 1] - x[i])`.

## Сборка и запуск

```sh
make
make run
```

При сборке `integral` библиотека явно передаётся линкеру:

```sh
gcc ... integral.c build/libdistributed_integral.a -o build/integral ...
```

Сборка с динамическим анализом:

```sh
make asan
make ubsan
make msan
```

Или напрямую:

```sh
make clean && make SAN=address
make clean && make SAN=undefined
make clean && make SAN=memory
```

`MemorySanitizer` собирается через `clang`.

Сборка через Clang без sanitizer:

```sh
make clang
```

Статический анализ:

```sh
make analyze
```

## Документация

HTML-документация генерируется через Doxygen:

```sh
make docs
```

Эта команда запускает:

```sh
doxygen Doxyfile
```

Готовая документация лежит в:

```text
docs/html/index.html
```

Откройте этот файл в браузере из каталога `06_distributed_integral`.
Очистить сгенерированный HTML можно командой:

```sh
make clean-docs
```

## Security Flags

Обычная GCC-сборка использует набор защитных флагов:

- `-Wall`, `-Wextra`, `-Wpedantic`, `-Werror`: включают строгие предупреждения и превращают их в ошибки сборки.
- `-fwrapv`: задаёт предсказуемое переполнение signed integer по модулю.
- `-fwrapv-pointer`: задаёт GCC-семантику wrapping для арифметики указателей.
- `-fno-strict-aliasing`: отключает оптимизации, основанные на строгих aliasing-правилах.
- `-fno-delete-null-pointer-checks`: запрещает удалять проверки на `NULL` после разыменования.
- `-D_FORTIFY_SOURCE=2`: включает дополнительные проверки libc для некоторых функций работы с памятью и строками.
- `-fstack-protector-strong`: добавляет stack canary для защиты от части переполнений стека.
- `-fPIE`, `-pie`: собирают position independent executable, пригодный для ASLR.
- `-fPIC`: генерирует position independent code.
- `-Wclobbered`, `-Warray-bounds`, `-Wdiv-by-zero`, `-Wshift-count-negative`, `-Wshift-count-overflow`: включают дополнительные проверки подозрительных конструкций.

`-O2` включён потому, что `_FORTIFY_SOURCE=2` требует оптимизации для полноценной работы. В Clang-сборке не используются GCC-only флаги `-fwrapv-pointer` и `-Wclobbered`.

Пример вручную:

```sh
./build/integral 0 0 3.141592653589793 1000 3
```

Аргументы `integral`:

```text
integral <func-id> <x-begin> <x-end> <num-points> <num-clients> [port [client-cores...]]
```

`func-id`: `0` для `sin(x)`, `1` для `cos(x)`, `2` для `x*x`.

Если нужно задать разное число ядер у worker, после порта передаётся ровно
`num-clients` чисел:

```sh
./build/integral 0 0 3.141592653589793 1000 4 1338 4 8 4 4
```

Здесь запускаются 4 клиента с весами `4`, `8`, `4`, `4`.

Для отдельного запуска `client` доступны дополнительные таймауты:

```text
client <host> <port> <cores> <worker-id> <connect-timeout-ms> <compute-timeout-ms>
```

`connect-timeout-ms` ограничивает ожидание подключения к server.
`compute-timeout-ms` ограничивает время
локального вычисления worker-а; если вычисление заняло больше времени, worker
завершается с ошибкой и не отправляет результат.

Для отдельного запуска `server` доступен общий таймаут:

```text
server <points-file> <num-clients> <port> <server-timeout-ms>
```

`server-timeout-ms` ограничивает время ожидания подключений и результатов от
worker-ов. На подключённых TCP-сокетах также включается `SO_KEEPALIVE`, но быстрые отказы в тестах
отслеживаются именно прикладными таймаутами.

## Запуск с внешними worker

`integral2` не создаёт worker-ы сам. Он генерирует файл точек и запускает
только `server`, поэтому worker-ы должны быть запущены заранее.

Готовый сценарий:

```sh
make test
```

Таймауты для автоматических тестов задаются переменными Makefile:

```sh
make test CONNECT_TIMEOUT_MS=5000 COMPUTE_TIMEOUT_MS=5000 SERVER_TIMEOUT_MS=5000 FAIL_TIMEOUT_MS=200
```

По умолчанию обычные сценарии используют `120000` мс, а тесты “не дождался”
используют `FAIL_TIMEOUT_MS=300` мс.

Скрипт `run_integral2_test.sh` сначала запускает клиентов, потом запускает
`integral2`.

`make test` также запускает сценарии отказа:

- `run_failure_test.sh worker ...`: один worker завершается после получения задачи; server детектирует отсутствие результата, но остальные worker-ы успевают досчитать, отправить результат и получить финальное подтверждение.
- `run_failure_test.sh server ...`: server завершается после рассылки задач; worker-ы завершаются с ошибкой при ожидании финального подтверждения.
- `tests/run_library_failure_test.sh worker ...`: запускает worker-ы через `build/library_workers`, а server через `build/library_server`; обе программы линкуются с `build/libdistributed_integral.a`. При отказе одного worker-а остальные worker-ы завершают вычисления, отправляют результаты серверу и получают финальное подтверждение, а server возвращает ошибку из-за отсутствующего результата.
- `tests/run_library_failure_test.sh server ...`: проверяет отказ server-а после рассылки задач. Все worker-ы завершаются с ошибкой, потому что не получают финальное подтверждение.
- `tests/run_timeout_test.sh client ...`: проверяет, что client завершается с ошибкой, если не дождался подключения к server.
- `tests/run_timeout_test.sh server ...`: проверяет, что server завершается с ошибкой, если не дождался подключения worker-ов.

Для fault-injection используются env-переменные:

- `INTEGRAL_FAIL_WORKER_ID=<id>` для отказа конкретного worker после получения задачи.
- `INTEGRAL_FAIL_SERVER_AFTER_TASKS=1` для отказа server после рассылки задач.

Пример вручную:

```sh
./build/client 127.0.0.1 1338 1 0 120000 120000 &
./build/client 127.0.0.1 1338 1 1 120000 120000 &
./build/client 127.0.0.1 1338 1 2 120000 120000 &
./build/integral2 0 0 3.141592653589793 30000000 3 1338
```

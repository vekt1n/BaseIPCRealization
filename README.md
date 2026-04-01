# Шина Сообщений (Shared Memory)

Реализация системы обмена данными между процессами с использованием **разделяемой памяти (shared memory)** и **очередей сообщений POSIX**. Включает в себя демоны для чтения, записи и логирования, а также примеры приложений с поддержкой подписок.

## Структура проекта

- **SharedMemory/** – библиотека для работы с разделяемой памятью (`BaseMemory.cpp`, `BaseMemory.h`).
  - `src/`
    - `BaseMemory.cpp` - сама шина сообщений
    - `Daemonizer.cpp` - Для демонизации некоторых программ
  - `include/`
    - `BaseMemory.hpp` - хедер
    - `Daemonizer.hpp` - хедер

- **SubscriptionAdapter/** – адаптер для механизма подписок (`Adapter.cpp`).
  - `Adapter.cpp` - брокер сообщений
- **daemons/** – исходные коды демонов:
  - `reader_daemon.cpp` – читает данные из разделяемой памяти.
  - `writer1_daemon.cpp` – записывает данные в разделяемую память (поток 1).
  - `writer2_daemon.cpp` – записывает данные в разделяемую память (поток 2).
  - `logger_daemon.cpp` – логирует события.
  - `ipc_manager.cpp` – утилита для управления IPC-ресурсами.
- **example/** – простые примеры без демонизации.
- **example_subscriptions/** – примеры с использованием подписок.
- **build/** – директория для собранных бинарных файлов (создаётся автоматически).

## Краткое описание основных сервисов

### BaseMemory.cpp

`BaseMemory.cpp` реализует класс для работы с разделяемой памятью (POSIX shm) как с кольцевой очередью сообщений фиксированного размера. Он управляет двумя очередями: своей (для чтения) и удалённой (для отправки). Ключевые методы:

- `createConnection()` – создание своей разделяемой памяти с синхронизацией для получения сообщений.
- `openConnection()` - открытие разделяемой памяти другого сервиса с синхронизацией через мьютексы и атомарные счётчики.
- `sendMessage()` – запись сообщения в очередь отправки (перегрузки для разных типов).
- `publishMessage()` – запись сообщения с тэгом для подписчиков (перегрузки для разных типов).
- `getMessage()` – чтение сообщения из своей очереди.
- `hasMessage()` / `hasSpace()` – проверка наличия сообщений или свободного места.
- `readOrNotMess()` – вспомогательная функция для обработки непрочитанных сообщений.

Используются `pthread_mutex_t` и `std::atomic` для потокобезопасности. Класс служит базой для демонов и примеров IPC.

### Daemonizer.cpp

`Daemonizer.cpp` реализует утилитарный класс для создания и управления демонами в Linux:

- **`daemonize()`** – выполняет стандартный двухкратный `fork()`, создаёт новую сессию (`setsid()`), закрывает стандартные дескрипторы и перенаправляет их в `/dev/null`, опционально записывает PID в файл.
- **`setupSignalHandlers()`** – устанавливает обработчики для сигналов завершения (SIGTERM, SIGINT, SIGQUIT), перезагрузки (SIGHUP) и игнорирует SIGPIPE.
- **`isDaemonRunning()`** – проверяет, существует ли процесс с PID из указанного файла, используя `kill(pid, 0)`, и удаляет устаревший файл при необходимости.
- **`writePidFile()`** / **`cleanupPidFile()`** – создают PID-файл с эксклюзивной блокировкой (`flock`), предотвращая запуск нескольких экземпляров, и удаляют его.
- **`switchToUser()`** – меняет UID/GID процесса на указанного пользователя для повышения безопасности.
- **`daemonLoop()`** – основной цикл демона, выполняющий переданную функцию, пока не придет сигнал остановки.
- **`reopenLogFiles()`** – заглушка для ротации логов по SIGHUP.

Класс использует глобальную переменную `daemon_running` для корректного завершения.

### Adapter.cpp

`Adapter.cpp` реализует брокер сообщений (pub/sub), работающий через разделяемую память. Основные функции:

- Создаёт `BaseMemory` с именем `/adapter` и обрабатывает входящие сообщения.
- При получении сообщения с тегом `"direct"` парсит команды `sub_to`/`unsub_to` для управления подписками (хранятся в `map<string, set<string>> subscription`).
- Для сообщений с другими тегами рассылает их всем подписчикам на соответствующий тег, открывая временное соединение с каждым подписчиком через `BaseMemory::openConnection()`.
- Поддерживает запуск в foreground (с выводом в консоль) или как демон (с использованием `Daemonizer`), обрабатывает сигналы для корректного завершения.

### ipc_manager.cpp

`ipc_manager.cpp` – утилита командной строки для управления демонами IPC (adapter, logger, reader, writer1, writer2). Основные возможности:

- **Запуск/остановка** отдельных демонов или всех (`start`/`stop`), с учётом корректного порядка (adapter → logger → reader → writers, обратный при остановке).
- **Рестарт** указанного демона.
- **Проверка статуса** (`status`) с отображением PID, uptime, детали из `/proc`.
- **Очистка ресурсов** (`cleanup`) – удаление PID-файлов, очередей и разделяемой памяти.
- Использует `system()` для запуска демонов с аргументами (`--foreground`, `--pid-file`), проверяет существование процесса через `kill(pid, 0)`.
- Хранит информацию о демонах в `std::map`, пути к PID-файлам, аргументы по умолчанию.

## Требования

- Компилятор с поддержкой **C++20** (g++).
- Операционная система Linux (используются POSIX shared memory и очереди).
- Установленные библиотеки `pthread` и `rt`.

## Сборка

Для сборки всех компонентов выполните:

```bash
mkdir build
make
```

Будут собраны:

- Демоны (`adapter`, `reader_daemon`, `writer1_daemon`, `writer2_daemon`, `logger_daemon`)
- Примеры (`reader_example`, `writer1_example`, `writer2_example`)
- Примеры с подписками (`sub_writer1`, `sub_writer2`, `sub_writer3`)
- Утилита `ipc_manager`

Все бинарные файлы помещаются в директорию `build/`.

### Сборка отдельных групп

- Только демоны: `make daemons`
- Только примеры: `make examples`
- Только примеры с подписками: `make examples_sub`
- Только утилиты: `make tools`

## Запуск

### Запуск демонов (фоновый режим)

После сборки демоны запускаются в фоновом режиме. Для этого используйте ipc_manager:
```bash
./build/ipc_manager # <=> help
```

получите такой вывод

```bash
IPC Manager - Control IPC daemons with Subscription Adapter
Usage: ipc_manager <command> [daemon] [options]

Commands:
  start [daemon]    Start daemon (or all if no daemon specified)
  stop [daemon]     Stop daemon (or all if no daemon specified)
  restart <daemon>  Restart specific daemon
  status [-d]       Show status of all daemons (-d for details)
  cleanup           Clean up shared memory and PID files
  help              Show this help

Daemons: adapter, logger, reader, writer1, writer2, all

Examples:
  ipc_manager start            # Start all daemons
  ipc_manager stop             # Stop all daemons
  ipc_manager start adapter    # Start only adapter
  ipc_manager status           # Show status
  ipc_manager status -d        # Show detailed status
```

Демоны самостоятельно создают необходимые IPC-объекты (очереди, разделяемую память).

### Запуск в режиме переднего плана (для отладки)

Для удобства отладки предусмотрены `make`-цели, которые запускают демоны в интерактивном режиме:

```bash
make run_adapter_fg
make run_reader_daemon_fg
make run_writer1_daemon_fg
make run_writer2_daemon_fg
make run_logger_daemon_fg
```

При запуске `logger_daemon` в фоне можно указать файл лога (по умолчанию `/var/log/shared_memory.log`), а в режиме переднего плана – через аргумент `--log-file ./test.log`.

### Примеры (без демонизации)

Примеры запускаются в разеых консолях:

```bash
make run_reader_example
make run_writer1_example
make run_writer2_example
```

Они демонстрируют базовое взаимодействие через разделяемую память без демонов.

### Примеры с подписками

Собранные примеры находятся в `build/`:

```bash
./build/ipc_adapter start adapter

./build/sub_writer1
# или
./build/sub_writer2
```

пишите в поле who?>: `/adapter`
а в поле mess?>: `sub_to <tag>` или `unsub_to <tag>` 

```bash
# пример
Connection created successfully
Dual-thread messenger started.
Type messages and press Enter to send.
Type 'exit' to quit.
who?> /adapter
mess?> sub_to tag1
# или
mess?> unsub_to tag1
```
потом запускаете `writer3 <mess> <tag>`

```bash
./build/sub_writer3 <mess> <tag>

# пример
./build/sub_writer3 Hello tag1
```

Эти примеры показывают, как подписываться на события в разделяемой памяти.

### Управление IPC-ресурсами

Утилита `ipc_manager` позволяет просматривать и очищать созданные очереди и разделяемую память:

```bash
./build/ipc_manager
```

Она выводит список активных ресурсов и предлагает их удалить.

## Очистка

- Удалить собранные бинарные файлы: `make clean`
- Удалить объекты разделяемой памяти (`/dev/shm/*`): `make free_mem`
- Выполнить обе операции: `make clean_all`

## Установка (создание служебных каталогов)

Для работы демонов в фоновом режиме требуются каталоги `/var/log` и `/var/run`. Если они отсутствуют, выполните:

```bash
make install_dirs
```

Эта команда создаст каталоги (если их нет) и установит права доступа к файлу лога.

## Примечания

- Демоны используют файлы `.pid` в `/var/run` для предотвращения множественного запуска.
- Логи пишутся в `/var/log/shared_memory.log` (если не указан другой файл).
- При завершении работы демонов рекомендуется использовать `ipc_manager` для освобождения ресурсов.

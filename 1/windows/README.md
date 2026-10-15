# Вариант 5 — Windows

Исходники: [SERVER/server.cpp](SERVER/server.cpp), [CLIENT/client.cpp](CLIENT/client.cpp). Для каждого есть отдельная программа с main. Протокол совместим с macOS-версией.

## Сборка и запуск

Все команды выполнять из папки windows. Нужны MSVC и Windows SDK, например из Visual Studio с компонентами разработки на C++. Открыть Developer PowerShell / Developer Command Prompt:

```powershell
cl /nologo /std:c++17 /EHsc /W4 /utf-8 SERVER\server.cpp /Feserver.exe /Foserver.obj /link ws2_32.lib
cl /nologo /std:c++17 /EHsc /W4 /utf-8 CLIENT\client.cpp /Feclient.exe /Foclient.obj /link ws2_32.lib
```

В первом терминале:

```powershell
.\server.exe
```

Во втором терминале из той же папки:

```powershell
.\client.exe
```

Конец консольного ввода (Ctrl+Z и Enter) также отправляет /quit.

При наличии CMake:

```powershell
cmake -S . -B build-cmake
cmake --build build-cmake --config Debug
```

Для генератора Visual Studio исполняемые файлы находятся в build-cmake\Debug; для Ninja — обычно в build-cmake.

## Команды для примера

После запуска клиента можно вводить:

```text
/history
/seed 42
/generate DIGITS 10
/generate LETTERS 8
/generate ALNUM 12
/generate PUNCT 6
/check DIGITS 12345
/check DIGITS 12a45
/history
/generate DIGITS 0
/generate UNKNOWN 4
/seed -1
/quit
```

Начальная история пуста. /generate выводит строку, /check — соответствие типу. Последние три ошибочные команды возвращают BAD_LENGTH, BAD_TYPE, BAD_SEED, после чего клиент продолжает работу. Конкретные случайные строки зависят от состояния генератора.

Для воспроизведения результата установить тот же seed и повторить те же команды /generate. /seed не очищает историю. После нового подключения генератор снова начинается с seed 5489, история пуста.

## Подключение и ограничения

Клиент по умолчанию использует 127.0.0.1:12345. Первым аргументом запуска можно передать IPv4-адрес другого компьютера. Сервер использует INADDR_ANY:12345; при работе по сети этот порт должен быть доступен.

Команды: /generate, /seed, /check, /history, /quit. Типы: DIGITS, LETTERS, ALNUM, PUNCT. Длина — 1–128, seed — 0–4294967295, числовые аргументы без знака. Буквы латинские, обоих регистров. Подробности в [протокол.md](протокол.md), разбор кода в [ПОЯСНЕНИЯ.md](ПОЯСНЕНИЯ.md).

Сервер работает с одним клиентом за раз. Сетевые вызовы блокирующие, таймаутов нет. /quit завершает сессию клиента; сервер останавливается Ctrl+C. Для сети используются только системные сокеты, остальные инструменты — стандартная библиотека C++17.

## Упаковка

Из этой папки:

```powershell
Compress-Archive -Path .\протокол.md,.\README.md,.\ПОЯСНЕНИЯ.md,.\CMakeLists.txt,.\SERVER,.\CLIENT -DestinationPath .\submission.zip -Force
```

Протокол и папки SERVER/CLIENT должны находиться прямо в корне архива. Результаты сборки не включать.

## Статус сборки

Эта версия не собиралась и не запускалась. Перед сдачей нужна сборка и запуск на Windows по инструкции выше.

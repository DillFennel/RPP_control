# Простой TCP-пример для Windows

Два исходника, как в презентации: [server/server.cpp](server/server.cpp) и [client/client.cpp](client/client.cpp). В каждом файле есть main и две короткие функции отправки/чтения строк. В сервере дополнительно есть handleCommand для логики задачи.

Протокол описан в [протокол.md](протокол.md). Команды: GET, SET, ADD, RESET, QUIT.

## Сборка

Все команды выполнять из этой папки.

Нужны MSVC и Windows SDK, например из Visual Studio с компонентами разработки на C++. Открыть Developer PowerShell / Developer Command Prompt:

```powershell
cl /nologo /std:c++17 /EHsc /W4 /utf-8 server\server.cpp /Feserver.exe /Foserver.obj /link ws2_32.lib
cl /nologo /std:c++17 /EHsc /W4 /utf-8 client\client.cpp /Feclient.exe /Foclient.obj /link ws2_32.lib
```

В одном терминале запустить `.\server.exe`, во втором — `.\client.exe`.

При наличии CMake:

```powershell
cmake -S . -B build-cmake
cmake --build build-cmake --config Debug
```

Для генератора Visual Studio программы находятся в build-cmake\\Debug, для Ninja обычно в build-cmake.

## Проверочная сессия

```text
GET
SET 10
ADD 5
ADD abc
GET
RESET
QUIT
```

Ожидается: 0, 10, 15, ошибка BAD_NUMBER, 15, 0, завершение сессии. При повторном подключении GET снова возвращает 0.

Клиент по умолчанию подключается к 127.0.0.1:12345. Можно передать IPv4-адрес другого компьютера первым аргументом. Сервер привязан к INADDR_ANY:12345; для обмена между компьютерами порт должен быть доступен через сеть.

QUIT закрывает клиентскую сессию. Конец консольного ввода (Ctrl+Z и Enter) тоже отправляет QUIT. Сервер останавливается обычным Ctrl+C, после чего ОС освобождает ресурсы процесса.

## Что понимать

- Сокеты создаются и закрываются явно.
- На сервере порядок socket → bind → listen → accept.
- На клиенте порядок socket → connect.
- receiveLine читает по одному байту до LF; recv может блокироваться.
- sendLine досылает всю строку, если send принял только часть.
- В сервере handleCommand проверяет аргументы и изменяет value.
- Клиент различает OK VALUE, OK BYE и ERR.
- Сервер обрабатывает одного клиента за раз; таймаутов ожидания нет.
- Формат чисел допускает плюс и минус. Неверный формат и выход за диапазон дают BAD_NUMBER.

Чтение по одному байту выбрано ради понятности на защите. Для больших потоков данных следует читать блоками; для этих коротких команд это учебный компромисс.

## Упаковка

Из этой папки:

```powershell
Compress-Archive -Path .\протокол.md,.\README.md,.\CMakeLists.txt,.\server,.\client -DestinationPath .\submission.zip -Force
```

В архиве протокол, server и client окажутся в корне. Бинарные файлы и папка сборки не нужны.

## Проверки

Актуальная Winsock-версия в этой среде не собиралась и не запускалась: нужна проверка на Windows.

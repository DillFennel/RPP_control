#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <sstream>
#include <string>

// TCP передаёт поток байтов. Разделяем сообщения символом '\n'.
enum ReadResult { LINE_OK, CLOSED, IO_ERROR, TOO_LONG, INCOMPLETE };
const std::size_t MAX_LINE = 1024;

// Читаем по одному байту: так не заберём начало следующей команды.
ReadResult receiveLine(SOCKET sock, std::string& line) {
    line.clear();
    for (;;) {
        char c;
        const int n = recv(sock, &c, 1, 0);
        if (n == 0) return line.empty() ? CLOSED : INCOMPLETE;
        if (n < 0) {
            return IO_ERROR;
        }
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return LINE_OK; // Принимаем и LF, и CRLF.
        }
        line += c;
        if (line.size() > MAX_LINE &&
            !(line.size() == MAX_LINE + 1 && c == '\r')) return TOO_LONG;
    }
}

// send может отправить часть строки: повторяем для оставшихся байтов.
bool sendLine(SOCKET sock, const std::string& line) {
    const std::string data = line + '\n';
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(sock, data.data() + sent,
            static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: client.exe [server_ipv4]\n";
        return 1;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
    // 1. Создаём сокет клиента.
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket failed\n";
        WSACleanup();
        return 1;
    }

    // 2. Указываем УДАЛЁННЫЙ адрес сервера.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(12345);
    const char* ip = argc == 2 ? argv[1] : "127.0.0.1";
    if (inet_pton(AF_INET, ip, &address.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address\n";
        closesocket(sock); WSACleanup();
        return 1;
    }
    // Свой локальный порт клиент получает автоматически при connect.
    if (connect(sock, (const sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "connect failed: start the server first\n";
        closesocket(sock); WSACleanup();
        return 1;
    }
    std::cout << "GET | SET <integer> | ADD <integer> | RESET | QUIT\n";
    int exitCode = 0;

    // 3. Читаем команду с клавиатуры, отправляем и разбираем ответ.
    for (;;) {
        std::cout << "> " << std::flush;
        std::string command;
        bool endOfInput = !std::getline(std::cin, command);
        if (std::cin.bad()) {
            std::cerr << "Console input error\n";
            exitCode = 1;
            break;
        }
        if (endOfInput) command = "QUIT"; // Ctrl+Z и Enter.
        if (command.size() > MAX_LINE) {
            std::cerr << "Maximum 1024 bytes per command\n";
            continue;
        }
        if (!sendLine(sock, command)) {
            std::cerr << "send failed\n";
            exitCode = 1;
            break;
        }

        std::string response;
        if (receiveLine(sock, response) != LINE_OK) {
            std::cerr << "Connection lost or incomplete/invalid response\n";
            exitCode = 1;
            break;
        }
        if (response == "OK BYE") {
            std::cout << "Session finished\n";
            break;
        }

        std::istringstream input(response);
        std::string status, type, extra;
        long long value;
        input >> status >> type;
        if (status == "OK" && type == "VALUE" &&
            (input >> value) && !(input >> extra)) {
            std::cout << "Value: " << value << '\n';
        } else if (status == "ERR" && !type.empty()) {
            std::string description;
            std::getline(input >> std::ws, description);
            if (description.empty()) {
                std::cerr << "Invalid error response\n";
                exitCode = 1;
                break;
            }
            std::cout << "Server error [" << type << "]: " << description << '\n';
        } else {
            std::cerr << "Invalid server response\n";
            exitCode = 1;
            break;
        }
        if (endOfInput) {
            std::cerr << "Server did not confirm QUIT\n";
            exitCode = 1;
            break;
        }
    }
    // 4. Освобождаем ресурсы явно, как в презентации.
    shutdown(sock, SD_SEND);
    closesocket(sock); WSACleanup();
    return exitCode;
}

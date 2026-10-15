#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <sstream>
#include <string>
#include <climits>

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

// Здесь меняется логика под индивидуальный вариант.
// value принадлежит одному соединению; quit завершает только эту сессию.
std::string handleCommand(const std::string& line, long long& value, bool& quit) {
    for (unsigned char c : line) {
        if (c != '\t' && (c < 32 || c > 126))
            return "ERR BAD_TEXT Use_printable_ASCII_or_tab";
    }
    std::istringstream input(line);
    std::string command, argument, extra;
    if (!(input >> command)) return "ERR EMPTY_COMMAND Command_is_empty";

    if (command == "GET" || command == "RESET" || command == "QUIT") {
        if (input >> extra) return "ERR BAD_ARGUMENTS Expected_no_arguments";
        if (command == "QUIT") {
            quit = true;
            return "OK BYE";
        }
        if (command == "RESET") value = 0;
        return "OK VALUE " + std::to_string(value);
    }
    if (command != "SET" && command != "ADD")
        return "ERR UNKNOWN_COMMAND Use_GET_SET_ADD_RESET_QUIT";
    if (!(input >> argument) || (input >> extra))
        return "ERR BAD_ARGUMENTS Expected_one_integer";

    long long number;
    std::istringstream numberInput(argument);
    if (!(numberInput >> number) || (numberInput >> extra))
        return "ERR BAD_NUMBER Expected_integer_in_int64_range";

    if (command == "SET") {
        value = number;
    } else {
        // Проверяем ДО сложения, чтобы не допустить переполнения.
        if ((number > 0 && value > LLONG_MAX - number) ||
            (number < 0 && value < LLONG_MIN - number))
            return "ERR OVERFLOW Result_does_not_fit_int64";
        value += number;
    }
    return "OK VALUE " + std::to_string(value);
}

int main() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
    // 1. Создаём TCP-сокет, как в примере из презентации.
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "socket failed\n";
        WSACleanup();
        return 1;
    }

    // 2. Заполняем локальный адрес, затем привязываем его к сокету.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(12345);
    if (bind(serverSocket, (const sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "bind failed: port 12345 may be busy\n";
        closesocket(serverSocket); WSACleanup();
        return 1;
    }
    if (listen(serverSocket, 3) < 0) {
        std::cerr << "listen failed\n";
        closesocket(serverSocket); WSACleanup();
        return 1;
    }
    std::cout << "Server on port 12345. Ctrl+C to stop.\n";

    // 3. По очереди принимаем клиентов.
    for (;;) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept failed\n";
            break;
        }
        std::cout << "Client connected\n";
        long long value = 0; // Новое соединение начинает с нуля.
        bool quit = false;
        while (!quit) {
            std::string request;
            ReadResult result = receiveLine(clientSocket, request);
            if (result == CLOSED) break;
            if (result == IO_ERROR) {
                std::cerr << "recv failed\n";
                break;
            }
            if (result == TOO_LONG || result == INCOMPLETE) {
                sendLine(clientSocket, result == TOO_LONG
                    ? "ERR LINE_TOO_LONG Maximum_1024_bytes"
                    : "ERR INCOMPLETE_MESSAGE Expected_newline");
                break; // Ошибка framing завершает эту сессию.
            }
            std::string reply = handleCommand(request, value, quit);
            if (!sendLine(clientSocket, reply)) {
                std::cerr << "send failed\n";
                break;
            }
        }
        // 4. Закрываем клиентский сокет и возвращаемся к accept.
        shutdown(clientSocket, SD_SEND);
        closesocket(clientSocket);
        std::cout << "Client disconnected\n";
    }
    closesocket(serverSocket); WSACleanup();
    return 1; // Сюда попадаем при ошибке accept.
}

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// TCP передаёт поток байтов. Разделяем сообщения символом '\n'.
enum ReadResult { LINE_OK, CLOSED, IO_ERROR, TOO_LONG, INCOMPLETE };
const std::size_t MAX_LINE = 1024;

// Читаем по одному байту: так не заберём начало следующей команды.
ReadResult receiveLine(int sock, std::string& line) {
    line.clear();
    for (;;) {
        char c;
        const ssize_t n = recv(sock, &c, 1, 0);
        if (n == 0) return line.empty() ? CLOSED : INCOMPLETE;
        if (n < 0) {
            if (errno == EINTR) continue;
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
bool sendLine(int sock, const std::string& line) {
    const std::string data = line + '\n';
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(sock, data.data() + sent,
            data.size() - sent, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// Разрешены только десятичные цифры; переполнение обнаружит поток ввода.
bool parseUnsigned(const std::string& text, unsigned long long& number) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    std::istringstream input(text);
    return static_cast<bool>(input >> number);
}

// Строки результата не содержат пробелов, поэтому читаются одним токеном.
bool validValue(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    for (unsigned char c : value) {
        if (c < 33 || c > 126) return false;
    }
    return true;
}

// Разбираем ответ сервера и показываем человеку результат.
bool printResponse(const std::string& response) {
    for (unsigned char c : response) {
        if (c != '\t' && (c < 32 || c > 126)) return false;
    }
    std::istringstream input(response);
    std::string status, type, argument, extra;
    if (!(input >> status >> type)) return false;
    if (status == "ERR") {
        std::string description;
        std::getline(input >> std::ws, description);
        if (description.empty()) return false;
        std::cout << "Server error [" << type << "]: " << description << '\n';
        return true;
    }
    if (status != "OK") return false;

    if (type == "HISTORY") {
        unsigned long long count;
        if (!(input >> argument) || !parseUnsigned(argument, count) || count > 5)
            return false;
        std::vector<std::string> history;
        for (unsigned long long i = 0; i < count; ++i) {
            std::string value;
            if (!(input >> value) || !validValue(value)) return false;
            history.push_back(value);
        }
        if (input >> extra) return false;
        if (history.empty()) std::cout << "History is empty\n";
        for (std::size_t i = 0; i < history.size(); ++i)
            std::cout << i + 1 << ": " << history[i] << '\n';
        return true;
    }

    if (!(input >> argument) || (input >> extra)) return false;
    if (type == "STRING" && validValue(argument)) {
        std::cout << "Generated: " << argument << '\n';
        return true;
    }
    if (type == "SEED") {
        unsigned long long seed;
        if (!parseUnsigned(argument, seed) || seed > 4294967295ULL) return false;
        std::cout << "Seed: " << seed << '\n';
        return true;
    }
    if (type == "CHECK" && (argument == "YES" || argument == "NO")) {
        std::cout << (argument == "YES" ? "Matches the type\n" : "Does not match the type\n");
        return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: ./client [server_ipv4]\n";
        return 1;
    }
    std::signal(SIGPIPE, SIG_IGN);
    // 1. Создаём сокет клиента.
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "socket failed\n";
        return 1;
    }

    // 2. Указываем УДАЛЁННЫЙ адрес сервера.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(12345);
    const char* ip = argc == 2 ? argv[1] : "127.0.0.1";
    if (inet_pton(AF_INET, ip, &address.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address\n";
        close(sock);
        return 1;
    }
    // Свой локальный порт клиент получает автоматически при connect.
    if (connect(sock, (const sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "connect failed: start the server first\n";
        close(sock);
        return 1;
    }
    std::cout << "/generate <type> <length> | /seed <number> | /check <type> <text> | /history | /quit\nTypes: DIGITS LETTERS ALNUM PUNCT; length: 1..128\n";
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
        if (endOfInput) command = "/quit"; // Ctrl+D в пустой строке.
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

        if (!printResponse(response)) {
            std::cerr << "Invalid server response\n";
            exitCode = 1;
            break;
        }
        if (endOfInput) {
            std::cerr << "Server did not confirm /quit\n";
            exitCode = 1;
            break;
        }
    }
    // 4. Освобождаем ресурсы явно, как в презентации.
    shutdown(sock, SHUT_WR);
    close(sock);
    return exitCode;
}

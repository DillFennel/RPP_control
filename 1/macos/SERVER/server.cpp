#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>

#include <cerrno>
#include <iostream>
#include <sstream>

#include <string>
#include <random>
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

std::string alphabetForType(const std::string& type) {
    if (type == "DIGITS") return "0123456789";
    if (type == "LETTERS") return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    if (type == "ALNUM") return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    // \" обозначает кавычку, \\ обозначает одну обратную косую черту.
    if (type == "PUNCT") return "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    return ""; // Неизвестный тип.
}

// Генератор и история принадлежат одному соединению и передаются по ссылке.
std::string handleCommand(const std::string& line, std::mt19937& generator,
                         std::vector<std::string>& history, bool& quit) {
    for (unsigned char c : line) {
        if (c != '\t' && (c < 32 || c > 126))
            return "ERR BAD_FORMAT Use_printable_ASCII_or_tab";
    }
    std::istringstream input(line);
    std::string command, type, argument, extra;
    if (!(input >> command)) return "ERR BAD_FORMAT Command_is_empty";

    if (command == "/quit" || command == "/history") {
        if (input >> extra) return "ERR BAD_FORMAT Expected_no_arguments";
        if (command == "/quit") {
            quit = true;
            return "OK BYE";
        }
        std::string reply = "OK HISTORY " + std::to_string(history.size());
        for (const std::string& value : history) reply += " " + value;
        return reply;
    }

    if (command == "/seed") {
        if (!(input >> argument) || (input >> extra))
            return "ERR BAD_FORMAT Expected_one_seed";
        unsigned long long seed;
        if (!parseUnsigned(argument, seed) || seed > 4294967295ULL)
            return "ERR BAD_SEED Expected_0_to_4294967295";
        generator.seed(static_cast<std::mt19937::result_type>(seed));
        return "OK SEED " + std::to_string(seed);
    }

    if (command != "/generate" && command != "/check")
        return "ERR UNKNOWN_COMMAND Use_generate_seed_check_history_quit";
    if (!(input >> type >> argument) || (input >> extra))
        return "ERR BAD_FORMAT Expected_type_and_argument";
    const std::string alphabet = alphabetForType(type);
    if (alphabet.empty()) return "ERR BAD_TYPE Use_DIGITS_LETTERS_ALNUM_PUNCT";

    if (command == "/check") {
        if (argument.size() > 128) return "ERR BAD_LENGTH Expected_1_to_128";
        for (char c : argument) {
            if (alphabet.find(c) == std::string::npos) return "OK CHECK NO";
        }
        return "OK CHECK YES";
    }

    unsigned long long length;
    if (!parseUnsigned(argument, length) || length == 0 || length > 128)
        return "ERR BAD_LENGTH Expected_1_to_128";
    // Для каждого символа выбираем случайный индекс в алфавите.
    std::uniform_int_distribution<int> pick(0, static_cast<int>(alphabet.size()) - 1);
    std::string value;
    for (unsigned long long i = 0; i < length; ++i) {
        value += alphabet[static_cast<std::size_t>(pick(generator))];
    }
    history.push_back(value);
    if (history.size() > 5) history.erase(history.begin());
    return "OK STRING " + value;
}

int main() {
    // Запись в закрытое соединение должна дать ошибку send, а не убить сервер.
    std::signal(SIGPIPE, SIG_IGN);
    // Создаем TCP-сокет
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "socket failed\n";
        return 1;
    }
    // Без этого порт какое-то время будет в таймауте после отсановке сервера. Так можно сразу переиспользовать
    const int reuse = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        close(serverSocket);
        return 1;
    }

    // Заполняем адрес
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(12345);
    // Привязываем сокет к адресу
    if (bind(serverSocket, (const sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "bind failed: port 12345 may be busy\n";
        close(serverSocket);
        return 1;
    }
    if (listen(serverSocket, 3) < 0) {
        std::cerr << "listen failed\n";
        close(serverSocket);
        return 1;
    }
    std::cout << "Server on port 12345. Ctrl+C to stop.\n";

    // 3. По очереди принимаем клиентов.
    while (true) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket < 0) {
            if (errno == EINTR) continue;
            std::cerr << "accept failed\n";
            break;
        }
        std::cout << "Client connected\n";
        std::mt19937 generator(5489u); // Генератор этой сессии.
        std::vector<std::string> history; // Не больше пяти результатов.
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
            std::string reply = handleCommand(request, generator, history, quit);
            if (!sendLine(clientSocket, reply)) {
                std::cerr << "send failed\n";
                break;
            }
        }
        // 4. Закрываем клиентский сокет и возвращаемся к accept.
        shutdown(clientSocket, SHUT_WR);
        close(clientSocket);
        std::cout << "Client disconnected\n";
    }
    close(serverSocket);
    return 1; // Сюда попадаем при ошибке accept.
}

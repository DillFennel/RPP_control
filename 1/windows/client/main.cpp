#include "../common/network.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: client.exe [server_ipv4]\n";
        return 1;
    }
    const char* server_ip = argc == 2 ? argv[1] : "127.0.0.1";
    try {
        net::Winsock winsock;
        net::Socket connection(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (connection.get() == INVALID_SOCKET) {
            std::cerr << "socket failed: " << WSAGetLastError() << '\n';
            return 1;
        }

        // Здесь указан УДАЛЁННЫЙ адрес сервера.
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(protocol::PORT);
        if (inet_pton(AF_INET, server_ip, &address.sin_addr) != 1) {
            std::cerr << "Invalid IPv4 address. Example: 127.0.0.1\n";
            return 1;
        }

        // bind не нужен: connect автоматически назначит локальный адрес и порт.
        if (connect(connection.get(), reinterpret_cast<const sockaddr*>(&address),
                    sizeof(address)) == SOCKET_ERROR) {
            std::cerr << "connect failed: " << WSAGetLastError() << '\n';
            return 1;
        }
        int error = 0;
        if (!net::set_send_timeout(connection.get(), error)) {
            std::cerr << "setsockopt failed: " << error << '\n';
            return 1;
        }
        std::cout << "Connected to " << server_ip << ':' << protocol::PORT << '\n'
                  << "Commands: GET | SET <integer> | ADD <integer> | RESET | QUIT\n";
        std::string pending, command, line;

        for (;;) {
            std::cout << "> " << std::flush;
            bool input_closed = false;
            if (!std::getline(std::cin, command)) {
                // Ctrl+Z, затем Enter в консоли Windows: завершаем сессию через QUIT.
                if (std::cin.bad()) {
                    std::cerr << "Console input error.\n";
                    return 1;
                }
                input_closed = true;
                command = "QUIT";
            }
            if (command.size() > protocol::MAX_LINE || !protocol::valid_text(command)) {
                std::cerr << "Use ASCII text, maximum 1024 bytes.\n";
                continue;
            }

            // Ровно один запрос, затем один ответ. send_line добавляет LF.
            if (!net::send_line(connection.get(), command, error)) {
                std::cerr << "send failed: " << error << '\n';
                return 1;
            }
            const auto result = net::read_line(connection.get(), pending, line, error);
            if (result != net::ReadResult::Line) {
                if (result == net::ReadResult::Error) {
                    std::cerr << "Receive failed, WSA error: " << error << '\n';
                } else {
                    std::cerr << "Server closed connection or sent an invalid frame.\n";
                }
                return 1;
            }

            // ТОЧКА АДАПТАЦИИ №3: разбор и отображение новых видов ответов.
            const auto reply = protocol::parse_reply(line);
            if (reply.kind == protocol::ReplyKind::Value) {
                std::cout << "Value: " << reply.value << '\n';
            } else if (reply.kind == protocol::ReplyKind::Error) {
                std::cout << "Server error [" << reply.code << "]: "
                          << reply.description << '\n';
            } else if (reply.kind == protocol::ReplyKind::Bye) {
                std::cout << "Session finished.\n";
                shutdown(connection.get(), SD_SEND);
                return 0;
            } else {
                std::cerr << "Invalid server response: " << line << '\n';
                return 1;
            }
            if (input_closed) {
                std::cerr << "Server did not confirm QUIT. Closing connection.\n";
                return 1;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "../common/network.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: ./client [server_ipv4]\n";
        return 1;
    }
    const char* server_ip = argc == 2 ? argv[1] : "127.0.0.1";
    try {
        net::Socket connection(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (connection.get() == -1) {
            std::cerr << "socket failed: " << net::error_text(errno) << '\n';
            return 1;
        }

        // УДАЛЁННЫЙ адрес сервера.
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(protocol::PORT);
        if (inet_pton(AF_INET, server_ip, &address.sin_addr) != 1) {
            std::cerr << "Invalid IPv4 address. Example: 127.0.0.1\n";
            return 1;
        }
        // ОС автоматически назначает клиенту локальный адрес и порт.
        if (connect(connection.get(), reinterpret_cast<const sockaddr*>(&address),
                    sizeof(address)) == -1) {
            std::cerr << "connect failed: " << net::error_text(errno) << '\n';
            return 1;
        }
        int error = 0;
        if (!net::set_send_options(connection.get(), error)) {
            std::cerr << "setsockopt failed: " << net::error_text(error) << '\n';
            return 1;
        }
        std::cout << "Connected to " << server_ip << ':' << protocol::PORT << '\n'
                  << "Commands: GET | SET <integer> | ADD <integer> | RESET | QUIT\n";
        std::string pending, command, line;

        for (;;) {
            std::cout << "> " << std::flush;
            bool input_closed = false;
            if (!std::getline(std::cin, command)) {
                // Ctrl+D в пустой строке терминала macOS: отправляем QUIT.
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
            if (!net::send_line(connection.get(), command, error)) {
                std::cerr << "send failed: " << net::error_text(error) << '\n';
                return 1;
            }
            const auto result = net::read_line(connection.get(), pending, line, error);
            if (result != net::ReadResult::Line) {
                if (result == net::ReadResult::Error) {
                    std::cerr << "Receive failed: " << net::error_text(error) << '\n';
                } else {
                    std::cerr << "Server closed connection or sent an invalid frame.\n";
                }
                return 1;
            }

            // ТОЧКА АДАПТАЦИИ №3: отображение новых видов ответов.
            const auto reply = protocol::parse_reply(line);
            if (reply.kind == protocol::ReplyKind::Value) {
                std::cout << "Value: " << reply.value << '\n';
            } else if (reply.kind == protocol::ReplyKind::Error) {
                std::cout << "Server error [" << reply.code << "]: "
                          << reply.description << '\n';
            } else if (reply.kind == protocol::ReplyKind::Bye) {
                std::cout << "Session finished.\n";
                shutdown(connection.get(), SHUT_WR);
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

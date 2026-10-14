#include "../common/network.hpp"
#include "commands.hpp"
#include <iostream>

void serve_client(int socket) {
    Session session; // Каждое соединение получает собственное значение 0.
    std::string pending; // Остаток потока между вызовами recv.
    std::string line;
    int error = 0;

    while (!net::stop_requested) {
        const auto result = net::read_line(socket, pending, line, error);
        if (result == net::ReadResult::Closed || result == net::ReadResult::Stopped) {
            break;
        }
        if (result == net::ReadResult::Error) {
            std::cerr << "Receive failed: " << net::error_text(error) << '\n';
            break;
        }
        if (result == net::ReadResult::TooLong || result == net::ReadResult::Incomplete) {
            const std::string reply = result == net::ReadResult::TooLong
                ? "ERR LINE_TOO_LONG Maximum_1024_bytes"
                : "ERR INCOMPLETE_MESSAGE Missing_newline_before_EOF";
            if (!net::send_line(socket, reply, error)) {
                std::cerr << "Cannot send error reply: " << net::error_text(error) << '\n';
            }
            break;
        }

        const auto response = handle_command(line, session);
        if (!net::send_line(socket, response.reply, error)) {
            std::cerr << "Send failed: " << net::error_text(error) << '\n';
            break;
        }
        if (response.close_connection) {
            break;
        }
    }
    shutdown(socket, SHUT_WR); // Завершаем отправку. close вызовет RAII-обёртка.
}

int main() {
    try {
        int error = 0;
        if (!net::install_stop_handlers(error)) {
            std::cerr << "sigaction failed: " << net::error_text(error) << '\n';
            return 1;
        }

        // 1. Создаём TCP-сокет. POSIX не требует WSAStartup.
        net::Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listener.get() == -1) {
            std::cerr << "socket failed: " << net::error_text(errno) << '\n';
            return 1;
        }
        // Упрощает перезапуск сервера после закрытия предыдущих соединений.
        const int reuse = 1;
        if (setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                       &reuse, sizeof(reuse)) == -1) {
            std::cerr << "setsockopt failed: " << net::error_text(errno) << '\n';
            return 1;
        }

        // 2. bind задаёт НАШ адрес и порт: все локальные IPv4-интерфейсы, 12345.
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(protocol::PORT);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
                 sizeof(address)) == -1) {
            std::cerr << "bind failed: " << net::error_text(errno) << '\n';
            return 1;
        }

        // 3. Переводим сокет в режим приёма подключений.
        if (listen(listener.get(), SOMAXCONN) == -1) {
            std::cerr << "listen failed: " << net::error_text(errno) << '\n';
            return 1;
        }
        std::cout << "TCP server on port " << protocol::PORT
                  << ". One active client at a time. Ctrl+C to stop.\n";

        while (!net::stop_requested) {
            const int ready = net::wait_readable(listener.get(), error);
            if (ready == 0) {
                break;
            }
            if (ready < 0) {
                std::cerr << "select failed: " << net::error_text(error) << '\n';
                return 1;
            }
            // 4. accept возвращает отдельный сокет для конкретного клиента.
            net::Socket client(accept(listener.get(), nullptr, nullptr));
            if (client.get() == -1) {
                if (errno != EINTR) {
                    std::cerr << "accept failed: " << net::error_text(errno) << '\n';
                }
                continue;
            }
            if (!net::set_send_options(client.get(), error)) {
                std::cerr << "setsockopt failed: " << net::error_text(error) << '\n';
                continue;
            }
            // 5. Обслуживаем эту сессию до QUIT или отключения клиента.
            std::cout << "Client connected. New session: value = 0.\n";
            serve_client(client.get());
            std::cout << "Client session finished.\n";
        } // После сессии закрывается client, при выходе из main — listener.
        std::cout << "Server stopped.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

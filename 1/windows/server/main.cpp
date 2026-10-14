#include "../common/network.hpp"
#include "commands.hpp"
#include <iostream>

void serve_client(SOCKET socket) {
    Session session; // Новое соединение — новое состояние со значением 0.
    std::string pending; // Остаток TCP-потока между вызовами recv.
    std::string line;
    int error = 0;

    while (!net::stop_requested.load()) {
        const auto result = net::read_line(socket, pending, line, error);
        if (result == net::ReadResult::Closed || result == net::ReadResult::Stopped) {
            break;
        }
        if (result == net::ReadResult::Error) {
            std::cerr << "Receive failed, WSA error: " << error << '\n';
            break;
        }
        if (result == net::ReadResult::TooLong || result == net::ReadResult::Incomplete) {
            const std::string reply = result == net::ReadResult::TooLong
                ? "ERR LINE_TOO_LONG Maximum_1024_bytes"
                : "ERR INCOMPLETE_MESSAGE Missing_newline_before_EOF";
            // При обрыве сети отправить ошибку может быть уже невозможно.
            if (!net::send_line(socket, reply, error)) {
                std::cerr << "Cannot send error reply, WSA error: " << error << '\n';
            }
            break; // Нарушен framing: завершаем только эту сессию.
        }

        const CommandResult response = handle_command(line, session);
        if (!net::send_line(socket, response.reply, error)) {
            std::cerr << "Send failed, WSA error: " << error << '\n';
            break;
        }
        if (response.close_connection) {
            break;
        }
    }
    shutdown(socket, SD_SEND); // Завершение отправки; closesocket вызовет RAII.
}

int main() {
    try {
        net::Winsock winsock; // WSAStartup здесь, WSACleanup при выходе из main.
        if (!SetConsoleCtrlHandler(net::console_handler, TRUE)) {
            std::cerr << "Cannot install Ctrl+C handler: " << GetLastError() << '\n';
            return 1;
        }

        // 1. Создание TCP-сокета. В Windows тип SOCKET, ошибка INVALID_SOCKET.
        net::Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listener.get() == INVALID_SOCKET) {
            std::cerr << "socket failed: " << WSAGetLastError() << '\n';
            return 1;
        }

        // 2. Локальный адрес сервера. INADDR_ANY принимает через все интерфейсы.
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(protocol::PORT);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
                 sizeof(address)) == SOCKET_ERROR) {
            std::cerr << "bind failed: " << WSAGetLastError() << '\n';
            return 1;
        }

        // 3. Начинаем принимать соединения.
        if (listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "listen failed: " << WSAGetLastError() << '\n';
            return 1;
        }
        std::cout << "TCP server on port " << protocol::PORT
                  << ". One active client at a time. Ctrl+C to stop.\n";

        while (!net::stop_requested.load()) {
            int error = 0;
            const int ready = net::wait_readable(listener.get(), error);
            if (ready == 0) {
                break;
            }
            if (ready < 0) {
                std::cerr << "select failed: " << error << '\n';
                return 1;
            }

            // 4. accept возвращает НОВЫЙ сокет конкретного клиента.
            // listener продолжает существовать и сохраняет свой порт.
            net::Socket client(accept(listener.get(), nullptr, nullptr));
            if (client.get() == INVALID_SOCKET) {
                std::cerr << "accept failed: " << WSAGetLastError() << '\n';
                continue;
            }
            if (!net::set_send_timeout(client.get(), error)) {
                std::cerr << "setsockopt failed: " << error << '\n';
                continue;
            }

            // 5. Работаем с одним клиентом до QUIT или отключения.
            std::cout << "Client connected. New session: value = 0.\n";
            serve_client(client.get());
            std::cout << "Client session finished.\n";
        } // client закрывается в конце каждой итерации, listener — при выходе.
        std::cout << "Server stopped.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

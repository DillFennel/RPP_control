#pragma once

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "protocol.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>

namespace net {

// Обработчик сигнала только меняет флаг. Сокеты закрывает основной код.
inline volatile std::sig_atomic_t stop_requested = 0;
inline void signal_handler(int) { stop_requested = 1; }

inline bool install_stop_handlers(int& error) {
    struct sigaction action{};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    // Без SA_RESTART: системный вызов может вернуться с EINTR после сигнала.
    if (sigaction(SIGINT, &action, nullptr) == -1 ||
        sigaction(SIGTERM, &action, nullptr) == -1) {
        error = errno;
        return false;
    }
    return true;
}

inline std::string error_text(int error) {
    return std::to_string(error) + " (" + std::strerror(error) + ")";
}

// RAII: при выходе из области видимости деструктор закрывает дескриптор.
// В POSIX сокет — int; -1 означает ошибку. Дескриптор 0 тоже допустим.
class Socket {
    int handle_;
public:
    explicit Socket(int handle) : handle_(handle) {}
    ~Socket() {
        if (handle_ >= 0) {
            close(handle_);
        }
    }
    int get() const { return handle_; }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};

inline bool set_send_options(int socket, int& error) {
    // macOS: при записи в закрытое соединение получаем ошибку send,
    // а не завершение процесса из-за SIGPIPE.
    const int enabled = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE,
                   &enabled, sizeof(enabled)) == -1) {
        error = errno;
        return false;
    }
    // В POSIX SO_SNDTIMEO принимает timeval, а не DWORD, как в Windows.
    timeval timeout{};
    timeout.tv_sec = 5;
    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                   &timeout, sizeof(timeout)) == -1) {
        error = errno;
        return false;
    }
    return true;
}

// Перед recv/accept ждём готовности. Проверяем флаг остановки каждые 200 мс.
// Результат: 1 — готово, 0 — остановка, -1 — ошибка.
inline int wait_readable(int socket, int& error) {
    if (socket < 0 || socket >= FD_SETSIZE) {
        error = EINVAL; // FD_SET не допускает дескрипторы вне этого диапазона.
        return -1;
    }
    while (!stop_requested) {
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(socket, &sockets);
        timeval timeout{};
        timeout.tv_usec = 200000;
        // POSIX: первый аргумент — максимальный дескриптор плюс один.
        const int result = select(socket + 1, &sockets, nullptr, nullptr, &timeout);
        if (result == -1) {
            if (errno == EINTR) {
                continue; // Сигнал прервал ожидание; заново проверим stop_requested.
            }
            error = errno;
            return -1;
        }
        if (result > 0) {
            return 1;
        }
    }
    return 0;
}

// send может принять часть данных. Досылаем всё, продвигаясь на n байтов.
inline bool send_all(int socket, const std::string& data, int& error) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        if (stop_requested) {
            error = EINTR;
            return false;
        }
        const ssize_t n = send(socket, data.data() + sent, data.size() - sent, 0);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            error = errno;
            return false;
        }
        if (n == 0) {
            error = EPIPE;
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

inline bool send_line(int socket, const std::string& line, int& error) {
    return send_all(socket, line + '\n', error);
}

enum class ReadResult { Line, Closed, Incomplete, TooLong, Error, Stopped };

inline ReadResult read_line(int socket, std::string& pending,
                            std::string& line, int& error) {
    for (;;) {
        if (stop_requested) {
            return ReadResult::Stopped;
        }
        const auto parsed = protocol::extract_line(pending, line);
        if (parsed == protocol::LineResult::Ready) {
            return ReadResult::Line;
        }
        if (parsed == protocol::LineResult::TooLong) {
            return ReadResult::TooLong;
        }
        const int ready = wait_readable(socket, error);
        if (ready == 0) {
            return ReadResult::Stopped;
        }
        if (ready < 0) {
            return ReadResult::Error;
        }

        char buffer[512];
        const ssize_t n = recv(socket, buffer, sizeof(buffer), 0);
        if (n == 0) {
            return pending.empty() ? ReadResult::Closed : ReadResult::Incomplete;
        }
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            error = errno;
            return ReadResult::Error;
        }
        // recv не добавляет '\0': добавляем ровно n полученных байтов.
        pending.append(buffer, static_cast<std::size_t>(n));
    }
}

} // namespace net

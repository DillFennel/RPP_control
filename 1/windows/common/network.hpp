#pragma once

// winsock2.h подключается до windows.h. Ws2_32.lib добавлена в CMakeLists.txt.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "protocol.hpp"
#include <atomic>
#include <stdexcept>
#include <string>

namespace net {

// Обработчик Ctrl+C только выставляет флаг. Сокеты закрывает основной код.
inline std::atomic_bool stop_requested{false};
inline BOOL WINAPI console_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        stop_requested.store(true);
        return TRUE;
    }
    return FALSE;
}

// Два маленьких RAII-класса освобождают ресурсы при выходе из области видимости.
class Winsock {
public:
    Winsock() {
        WSADATA data{};
        const int error = WSAStartup(MAKEWORD(2, 2), &data);
        if (error != 0) {
            throw std::runtime_error("WSAStartup: " + std::to_string(error));
        }
    }
    ~Winsock() { WSACleanup(); }
    Winsock(const Winsock&) = delete;
    Winsock& operator=(const Winsock&) = delete;
};

class Socket {
    SOCKET handle_;
public:
    explicit Socket(SOCKET handle) : handle_(handle) {}
    ~Socket() {
        if (handle_ != INVALID_SOCKET) {
            closesocket(handle_);
        }
    }
    SOCKET get() const { return handle_; }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};

// Ограничиваем время блокировки send, если получатель перестал читать ответы.
inline bool set_send_timeout(SOCKET socket, int& error) {
    const DWORD timeout_ms = 5000;
    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms),
                   sizeof(timeout_ms)) == SOCKET_ERROR) {
        error = WSAGetLastError();
        return false;
    }
    return true;
}

// Перед recv/accept ждём готовности. Паузы по 200 мс позволяют заметить Ctrl+C.
// Результат: 1 — готово, 0 — остановка, -1 — сетевая ошибка.
inline int wait_readable(SOCKET socket, int& error) {
    while (!stop_requested.load()) {
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(socket, &sockets);
        timeval timeout{};
        timeout.tv_usec = 200000;
        const int result = select(0, &sockets, nullptr, nullptr, &timeout);
        if (result == SOCKET_ERROR) {
            error = WSAGetLastError();
            return -1;
        }
        if (result > 0) {
            return 1;
        }
    }
    return 0;
}

// send может принять только часть данных. Сдвигаем позицию и досылаем остаток.
inline bool send_all(SOCKET socket, const std::string& data, int& error) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        if (stop_requested.load()) {
            error = WSAEINTR;
            return false;
        }
        // Маленький блок гарантирует, что размер помещается в параметр int.
        const auto remaining = data.size() - sent;
        const int chunk = static_cast<int>(remaining > 4096 ? 4096 : remaining);
        const int n = send(socket, data.data() + sent, chunk, 0);
        if (n == SOCKET_ERROR) {
            error = WSAGetLastError();
            return false;
        }
        if (n == 0) {
            error = WSAECONNABORTED;
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

inline bool send_line(SOCKET socket, const std::string& line, int& error) {
    return send_all(socket, line + '\n', error);
}

enum class ReadResult { Line, Closed, Incomplete, TooLong, Error, Stopped };

inline ReadResult read_line(SOCKET socket, std::string& pending,
                            std::string& line, int& error) {
    for (;;) {
        if (stop_requested.load()) {
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
        const int n = recv(socket, buffer, sizeof(buffer), 0);
        if (n == 0) { // TCP: другая сторона штатно завершила отправку.
            return pending.empty() ? ReadResult::Closed : ReadResult::Incomplete;
        }
        if (n == SOCKET_ERROR) {
            error = WSAGetLastError();
            return ReadResult::Error;
        }
        // Используем n, а не strlen: recv не добавляет нулевой байт.
        pending.append(buffer, static_cast<std::size_t>(n));
    }
}

} // namespace net

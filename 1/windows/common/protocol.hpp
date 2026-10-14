#pragma once

#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace protocol {

constexpr unsigned short PORT = 12345;
constexpr std::size_t MAX_LINE = 1024; // Без завершающих LF или CRLF.

enum class LineResult { Ready, NeedMore, TooLong };

// TCP может принести половину строки или несколько строк одновременно.
// Забираем одну полную строку, остаток сохраняем для следующего вызова.
inline LineResult extract_line(std::string& pending, std::string& line) {
    const auto end = pending.find('\n');
    if (end == std::string::npos) {
        // Разрешаем ещё один байт: CR перед будущим LF.
        if (pending.size() > MAX_LINE + 1 ||
            (pending.size() == MAX_LINE + 1 && pending.back() != '\r')) {
            return LineResult::TooLong;
        }
        return LineResult::NeedMore;
    }

    auto length = end;
    if (length > 0 && pending[length - 1] == '\r') {
        --length;
    }
    if (length > MAX_LINE) {
        return LineResult::TooLong;
    }
    line = pending.substr(0, length);
    pending.erase(0, end + 1);
    return LineResult::Ready;
}

// В учебном протоколе команды, аргументы и ответы используют ASCII.
inline bool valid_text(std::string_view text) {
    for (unsigned char c : text) {
        if (c != '\t' && (c < 32 || c > 126)) {
            return false;
        }
    }
    return true;
}

enum class NumberResult { Ok, Invalid, OutOfRange };

// Допускаются цифры и необязательный минус. Знак '+' не допускается.
// from_chars проверяет число без исключений; проверяем также весь аргумент.
inline NumberResult parse_number(std::string_view text, std::int64_t& value) {
    if (text.empty()) {
        return NumberResult::Invalid;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ptr != text.data() + text.size()) {
        return NumberResult::Invalid;
    }
    if (result.ec == std::errc::result_out_of_range) {
        return NumberResult::OutOfRange;
    }
    return result.ec == std::errc{} ? NumberResult::Ok : NumberResult::Invalid;
}

enum class ReplyKind { Value, Bye, Error, Invalid };

struct Reply {
    ReplyKind kind = ReplyKind::Invalid;
    std::int64_t value = 0;
    std::string code;
    std::string description;
};

// Клиент разбирает ответ сервера, а не просто печатает сырые байты.
inline Reply parse_reply(const std::string& line) {
    Reply reply;
    if (!valid_text(line)) {
        return reply;
    }
    std::istringstream input(line);
    std::string status, type, number, extra;
    if (!(input >> status >> type)) {
        return reply;
    }
    if (status == "OK" && type == "VALUE") {
        if ((input >> number) && !(input >> extra) &&
            parse_number(number, reply.value) == NumberResult::Ok) {
            reply.kind = ReplyKind::Value;
        }
    } else if (status == "OK" && type == "BYE") {
        if (!(input >> extra)) {
            reply.kind = ReplyKind::Bye;
        }
    } else if (status == "ERR") {
        for (char c : type) {
            if ((c < 'A' || c > 'Z') && c != '_') {
                return reply;
            }
        }
        input >> std::ws;
        std::getline(input, reply.description);
        if (!reply.description.empty()) {
            reply.kind = ReplyKind::Error;
            reply.code = type;
        }
    }
    return reply;
}

} // namespace protocol

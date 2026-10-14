#pragma once

#include "../common/protocol.hpp"
#include <limits>
#include <sstream>
#include <string>

// ТОЧКА АДАПТАЦИИ №1: данные одной клиентской сессии.
struct Session {
    std::int64_t value = 0;
};

struct CommandResult {
    std::string reply;
    bool close_connection = false;
};

// ТОЧКА АДАПТАЦИИ №2: команды и логика индивидуального варианта.
// Сетевых вызовов здесь нет. Ошибка не должна изменять состояние.
inline CommandResult handle_command(const std::string& line, Session& session) {
    if (!protocol::valid_text(line)) {
        return {"ERR BAD_TEXT Use_printable_ASCII_or_tab"};
    }
    std::istringstream input(line);
    std::string command, argument, extra;
    if (!(input >> command)) {
        return {"ERR EMPTY_COMMAND Command_is_empty"};
    }
    if (command == "GET" || command == "RESET" || command == "QUIT") {
        if (input >> extra) {
            return {"ERR BAD_ARGUMENTS This_command_takes_no_arguments"};
        }
        if (command == "QUIT") {
            return {"OK BYE", true};
        }
        if (command == "RESET") {
            session.value = 0;
        }
        return {"OK VALUE " + std::to_string(session.value)};
    }
    if (command != "SET" && command != "ADD") {
        return {"ERR UNKNOWN_COMMAND Supported_GET_SET_ADD_RESET_QUIT"};
    }
    if (!(input >> argument) || (input >> extra)) {
        return {"ERR BAD_ARGUMENTS Expected_one_integer"};
    }
    std::int64_t number = 0;
    const auto parsed = protocol::parse_number(argument, number);
    if (parsed == protocol::NumberResult::Invalid) {
        return {"ERR BAD_NUMBER Expected_decimal_integer_without_plus"};
    }
    if (parsed == protocol::NumberResult::OutOfRange) {
        return {"ERR NUMBER_RANGE Integer_does_not_fit_int64"};
    }

    if (command == "SET") {
        session.value = number;
    } else {
        const auto maximum = (std::numeric_limits<std::int64_t>::max)();
        const auto minimum = (std::numeric_limits<std::int64_t>::min)();
        // Проверяем ДО сложения: переполнение signed integer недопустимо.
        if ((number > 0 && session.value > maximum - number) ||
            (number < 0 && session.value < minimum - number)) {
            return {"ERR OVERFLOW Addition_would_overflow_int64"};
        }
        session.value += number;
    }
    return {"OK VALUE " + std::to_string(session.value)};
}

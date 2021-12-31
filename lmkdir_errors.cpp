#include <iostream>
#include <sstream>

#include <eti.h>
#include "lmkdir_errors.hpp"

const char* get_menu_error_symbol(int code) noexcept {
    switch (code) {
    case 0:
        return "E_OK";
    case -1:
        return "E_SYSTEM_ERROR";
    case -2:
        return "E_BAD_ARGUMENT";
    case -3:
        return "E_POSTED";
    case -4:
        return "E_CONNECTED";
    case -5:
        return "E_BAD_STATE";
    case -6:
        return "E_NO_ROOM";
    case -7:
        return "E_NOT_POSTED";
    case -8:
        return "E_UNKNOWN_COMMAND";
    case -9:
        return "E_NO_MATCH";
    case -10:
        return "E_NOT_SELECTABLE";
    case -11:
        return "E_NOT_CONNECTED";
    case -12:
        return "E_REQUEST_DENIED";
    case -13:
        return "E_INVALID_FIELD";
    case -14:
        return "E_CURRENT";
    default:
        return "UNKNOWN";
    }
}

void error_out(std::string_view msg, int code, std::string_view file, long line) {
    std::ostringstream is;
    is << file << ':' << line << ": ";

    if (code != 0) {
        is << '(' << get_menu_error_symbol(code) << ") ";
    }
    is << msg;

    if (std::uncaught_exceptions() == 0) throw fatal_error{ is.str(), code };
}
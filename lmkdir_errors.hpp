#ifndef LMKDIR_ERRORS_HPP
#define LMKDIR_ERRORS_HPP

#include <string_view>
#include <stdexcept>

#define NO_ASSERT 0

#if NO_ASSERT != 0
#   define RUNTIME_CODE_ASSERT(cond, code)
#   define RUNTIME_MSG_ASSERT(cond, msg)
#else
#   define RUNTIME_CODE_ASSERT(cond, code) if (!(cond)) { error_out(#cond, code, __FILE__, __LINE__); }
#   define RUNTIME_MSG_ASSERT(cond, msg) if (!(cond)) { error_out(msg, 0, __FILE__, __LINE__); }
#endif

#define RUNTIME_ASSERT(cond) RUNTIME_CODE_ASSERT(cond, 0)
#define RUNTIME_ERROR(msg) error_out(msg, 0, __FILE__, __LINE__)

#define CHECK_OK(expr) { int res = (expr); (void)res; RUNTIME_CODE_ASSERT(res != ERR, res); }
#define CHECK_MENU_OK(expr) { int res = (expr); (void)res; RUNTIME_CODE_ASSERT(res == E_OK, res); }

struct fatal_error : public std::runtime_error {
    fatal_error(const std::string &msg, int code)
    :std::runtime_error{ msg },
     error_code{ code }
    {}

    const int error_code;
};

const char* get_menu_error_symbol(int code) noexcept;
void error_out(std::string_view msg, int code, std::string_view file, long line);

#endif // LMKDIR_ERRORS_HPP
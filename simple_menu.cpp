#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>
#include <boost/container/string.hpp>
#include <experimental/filesystem>

#include <curses.h>
#include <menu.h>
#include "error_translation.h"

#define NO_ASSERT 0

#if NO_ASSERT != 0
#   define RUNTIME_CODE_ASSERT(cond, code)
#else
#   define RUNTIME_CODE_ASSERT(cond, code) if (!(cond)) { error_out(#cond, code, __FILE__, __LINE__); }
#endif

#define RUNTIME_ASSERT(cond) RUNTIME_CODE_ASSERT(cond, -1)

#define CHECK_OK(expr) { int res = (expr); (void)res; RUNTIME_CODE_ASSERT(res != ERR, res); }
#define CHECK_MENU_OK(expr) { int res = (expr); (void)res; RUNTIME_CODE_ASSERT(res == E_OK, res); }

void error_out(std::string_view msg, int code, std::string_view file, long line) {
    endwin();
    std::cerr << "Assertion Failed at " << file << ":" << line << " with error code " << get_menu_error_symbol(code) << "\n"
              << "\t" << msg << "\n";
    std::abort();
}

int main() {
    const std::vector<std::string_view> choices = {
        "foo",
        "bar"
    };

    initscr();

    CHECK_OK(cbreak());
    CHECK_OK(noecho());
    CHECK_OK(keypad(stdscr, TRUE));

    const std::size_t n_choices = choices.size();
    std::vector<ITEM*> my_items(n_choices + 1, nullptr);

    for (std::size_t i = 0; i < n_choices; ++i) {
        auto item = new_item(choices[i].data(), "");
        RUNTIME_ASSERT(item != nullptr);
        my_items[i] = item;
    }

    auto my_menu = new_menu(my_items.data());
    RUNTIME_ASSERT(my_menu != nullptr);

    CHECK_OK(mvprintw(LINES - 2, 0, "F1 to Exit"));
    CHECK_MENU_OK(post_menu(my_menu));
    CHECK_OK(refresh());

    int c;
    boost::container::string char_buffer;
    char_buffer.reserve(1024);

    while((c = getch()) != KEY_F(1)) {
        switch(c) {
        case KEY_DOWN:
            menu_driver(my_menu, REQ_DOWN_ITEM);
            break;
        case KEY_UP:
            menu_driver(my_menu, REQ_UP_ITEM);
            break;
        case KEY_BACKSPACE:
            if (!char_buffer.empty()) {
                char_buffer.pop_back();
                CHECK_OK(mvdelch(LINES - 4, char_buffer.size()));
                CHECK_OK(refresh());
            }
            break;
        }

        if (isalnum(c)) {
            CHECK_OK(mvaddch(LINES - 4, char_buffer.size(), c));
            char_buffer += c;
            CHECK_OK(refresh());
        }
        else if (c == KEY_ENTER) {
            break;
        }
    }

    CHECK_MENU_OK(unpost_menu(my_menu));
    CHECK_MENU_OK(free_menu(my_menu));
    for (auto &item : my_items) {
        if (item != nullptr) {
            CHECK_MENU_OK(free_item(item));
        }
    }
    CHECK_OK(endwin());
}
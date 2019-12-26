#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
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
    while((c = getch()) != KEY_F(1)) {
        switch(c) {
        case KEY_DOWN:
            menu_driver(my_menu, REQ_DOWN_ITEM);
            break;
        case KEY_UP:
            menu_driver(my_menu, REQ_UP_ITEM);
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
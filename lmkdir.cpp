#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <experimental/filesystem>

#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/adaptor/tokenized.hpp>
#include <boost/range/combine.hpp>
#include <gsl/gsl>

#include <curses.h>
#include <menu.h>
#include "lmkdir_errors.hpp"

#define FAKE_CREATE_DIRECTORY 1

constexpr char const* const manifest_filename = "/mnt/d/lenno/Downloads/New/.RED/lmkdir_manifest";
constexpr int esc_char = 27;

using directory_manifest = std::vector<std::string>;

class manifest_manager {
    std::unordered_map<std::string, ITEM*> m_manifest;

public:
    manifest_manager(directory_manifest&& initial_names)
    {
        m_manifest.reserve(100);

        for (auto &name : initial_names) {
            add_name(std::move(name));
        }
    }

    ~manifest_manager() {
        for (auto &pair : m_manifest) {
            free_item(pair.second);
        }
    }

    manifest_manager(const manifest_manager&) = delete;
    manifest_manager &operator=(const manifest_manager&) = delete;

    template <typename T>
    void add_name(T &&name) {
        auto res = m_manifest.emplace(std::forward<T>(name), nullptr);
        if (res.second) {
            auto &pair = *res.first;
            pair.second = new_item(pair.first.c_str(), "");
            RUNTIME_ASSERT(pair.second != nullptr);
        }
    }

    inline const auto &range() const noexcept {
        return m_manifest;
    }
};

class menu_manager {
    std::vector<ITEM*> m_visible_items;
    std::string m_char_buffer;
    std::string m_status_bar;

    manifest_manager &m_manifest_manager;
    MENU* m_menu;
    bool m_posted = false;

    int status_bar_y;
    int sep2_y;
    int input_bar_y;
    int sep1_y;

    template <typename Func>
    void filter_items(Func &&func) {
        m_visible_items.clear();

        for (const auto &pair : m_manifest_manager.range()) {
            if (func(pair.first)) {
                m_visible_items.emplace_back(pair.second);
            }
        }

        m_visible_items.emplace_back(nullptr);
    }

    template <typename Func>
    void update(Func &&func) {
        if (m_posted) {
            CHECK_MENU_OK(unpost_menu(m_menu));
            m_posted = false;
        }

        move(sep1_y, 0);
        CHECK_OK(hline('-', COLS));
        move(sep2_y, 0);
        CHECK_OK(hline('=', COLS));

        move(input_bar_y, 0);
        clrtoeol();
        CHECK_OK(printw(m_char_buffer.c_str()));

        move(status_bar_y, 0);
        clrtoeol();
        CHECK_OK(printw(m_status_bar.c_str()));

        filter_items(func);
        if (m_visible_items.size() > 1) {
            CHECK_MENU_OK(set_menu_items(m_menu, m_visible_items.data()));
            CHECK_MENU_OK(post_menu(m_menu));
            m_posted = true;
        }

        CHECK_OK(refresh());
    }

    bool char_buffer_is_prefix(const std::string_view str) const {
        return str.find(m_char_buffer) != std::string_view::npos;
    }

public:
    menu_manager(manifest_manager &manifest_manager)
    :m_manifest_manager{ manifest_manager }
    {
        m_char_buffer.reserve(1024);
        m_visible_items.reserve(100);

        filter_items([](auto&){ return true; });

        m_menu = new_menu(m_visible_items.data());
        RUNTIME_ASSERT(m_menu != nullptr);

        status_bar_y = LINES - 2;
        sep2_y = LINES - 3;
        input_bar_y = LINES - 4;
        sep1_y = LINES - 5;
    }

    ~menu_manager() {
        if (m_posted) unpost_menu(m_menu);
        free_menu(m_menu);
    }

    menu_manager(const menu_manager&) = delete;
    menu_manager &operator=(const menu_manager&) = delete;

    std::optional<std::string> next() {
        m_char_buffer.clear();
        update([](auto&){ return true; });

        while (true) {
            const int c = getch();

            switch(c) {
            case esc_char:
                return std::nullopt;

            case KEY_DOWN:
                menu_driver(m_menu, REQ_DOWN_ITEM);
                break;
            case KEY_UP:
                menu_driver(m_menu, REQ_UP_ITEM);
                break;

            case int('\n'):
                {
                    ITEM* item = current_item(m_menu);
                    if (item != nullptr && m_visible_items.size() > 1) {
                        const char* name = item_name(item);
                        RUNTIME_ASSERT(name != nullptr);
                        return name;
                    }
                    else if (!m_char_buffer.empty()) {
                        return m_char_buffer;
                    }
                }
                break;

            case KEY_BACKSPACE:
                if (!m_char_buffer.empty()) {
                    m_char_buffer.pop_back();
                    update([this](const auto &str){ return this->char_buffer_is_prefix(str); });
                }
                break;

            default:
                {
                    if (isalnum(c) || c == '_' || c == ' ') {
                        m_char_buffer += c;
                        update([this](const auto &str){ return this->char_buffer_is_prefix(str); });
                    }
                }
            }
        }
    }

    void notify(const std::string &dirname, bool success) {
        if (success) {
            m_manifest_manager.add_name(dirname);
            m_status_bar = "Successfully created directory \"" + dirname + "\"";
        }
        else {
            m_status_bar = "Failed to create directory \"" + dirname + "\"";
        }
    }

};

std::string_view strip(std::string_view str) {
    auto offset = str.find_first_not_of(" \t");
    if (offset != std::string_view::npos) {
        str = str.substr(offset);
    }

    offset = str.find_last_not_of(" \t");
    if (offset != std::string_view::npos) {
        str = str.substr(0, offset + 1);
    }

    return str;
}

directory_manifest read_directory_manifest(const std::string_view filename) {
    std::string contents;

    {
        std::ifstream fs{ filename.data(), std::ios_base::binary | std::ios_base::ate };
        RUNTIME_ASSERT(fs);

        const auto filesize = fs.tellg();
        contents.resize(gsl::narrow<std::size_t>(filesize));

        fs.seekg(0);
        RUNTIME_ASSERT(fs);

        fs.read(contents.data(), filesize);
        RUNTIME_ASSERT(fs);
    }

    auto range_of_names = contents
                          | boost::adaptors::tokenized(boost::regex("[^\\r\\n]+"))
                          | boost::adaptors::transformed([](auto &rng) { return std::string_view{ &*rng.begin(), gsl::narrow<std::size_t>(rng.end() - rng.begin()) }; })
                          | boost::adaptors::transformed(strip);

    directory_manifest manifest{ range_of_names.begin(), range_of_names.end() };
    std::sort(manifest.begin(), manifest.end());

    auto new_end = std::unique(manifest.begin(), manifest.end());
    manifest.erase(new_end, manifest.end());

    return manifest;
}

void write_directory_manifest(const std::string_view filename, const manifest_manager &manifest_man) {
    std::ofstream fs{ filename.data(), std::ios_base::binary };
    RUNTIME_ASSERT(fs);

    for (const auto &pair : manifest_man.range()) {
        fs << pair.first << "\n";
        RUNTIME_ASSERT(fs);
    }

    fs.flush();
    RUNTIME_ASSERT(fs);
}

bool create_directory(const std::string_view dirname) {
#if FAKE_CREATE_DIRECTORY == 0
    namespace fs = std::experimental::filesystem;

    try {
        fs::create_directory(dirname.data());
    }
    catch (const fs::filesystem_error &err) {
        return false;
    }
#endif
    return true;
}

void lmkdir() {
    struct screen_init_ {
        screen_init_() {
            initscr();
            CHECK_OK(cbreak());
            CHECK_OK(noecho());
            CHECK_OK(keypad(stdscr, TRUE));
        }
        ~screen_init_() { endwin(); }
    } screen_init;

    manifest_manager manifest_man{ read_directory_manifest(manifest_filename) };
    menu_manager menu_man{ manifest_man };

    while (auto dir_opt = menu_man.next()) {
        bool success = create_directory(*dir_opt);
        menu_man.notify(*dir_opt, success);
    }

    write_directory_manifest(manifest_filename, manifest_man);
}

int main() {
    try {
        lmkdir();
    }
    catch (const fatal_error &err) {
        std::cerr << err.what();
        return err.error_code;
    }

    return 0;
}
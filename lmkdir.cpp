#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <filesystem>

#include <boost/algorithm/string/find.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/adaptor/tokenized.hpp>
#include <boost/range/combine.hpp>
#include <gsl/gsl>

#include <curses.h>
#include <menu.h>
#include "lmkdir_errors.hpp"
#include "levenshtein.hpp"

#define FAKE_CREATE_DIRECTORY 0
#define USE_LEVENSHTEIN 1

constexpr char const* const manifest_filename = "lmkdir_manifest";
constexpr int esc_char = 27;

namespace fs = std::filesystem;
using directory_manifest = std::vector<std::string>;

class manifest_manager {
    std::unordered_set<std::string> m_manifest;
    std::unordered_map<ITEM*, std::string_view> m_items;

public:
    manifest_manager(directory_manifest&& initial_names)
    {
        m_manifest.reserve(initial_names.size());
        m_items.reserve(initial_names.size());

        for (auto &name : initial_names) {
            add_name(std::move(name));
        }

        initial_names.clear();
    }

    ~manifest_manager() {
        for (auto &pair : m_items) {
            free_item(pair.first);
        }
    }

    manifest_manager(const manifest_manager&) = delete;
    manifest_manager &operator=(const manifest_manager&) = delete;

    template <typename T>
    void add_name(T &&name) {
        auto [name_iter, is_new_name] = m_manifest.emplace(std::forward<T>(name));
        if (is_new_name) {
            auto [item_iter, item_inserted] = m_items.emplace(new_item(name_iter->c_str(), ""), *name_iter);
            RUNTIME_ASSERT(item_iter->first != nullptr);
            RUNTIME_ASSERT(item_inserted);
        }
    }

    inline const auto &range() const noexcept {
        return m_items;
    }

    inline std::size_t size() const noexcept {
        return m_items.size();
    }
};

class menu_manager {
    std::vector<ITEM*> m_visible_items;
    std::vector<ITEM*> m_items_back_buffer;
    std::vector<std::size_t> m_levenshtein_buffer;
    std::string m_char_buffer;
    std::string m_status_bar;

    manifest_manager &m_manifest_manager;
    MENU* m_menu;
    ITEM* m_curr_item;
    bool m_posted = false;

    int status_bar_y;
    int sep2_y;
    int input_bar_y;
    int sep1_y;

    void post_all_items() {
        std::swap(m_visible_items, m_items_back_buffer);

        m_visible_items.clear();
        m_visible_items.emplace_back(m_curr_item);
        
        for (const auto &pair : m_manifest_manager.range()) {
            m_visible_items.emplace_back(pair.first);
        }

        m_visible_items.emplace_back(nullptr);
    }

    template <typename PostFunc>
    void update(PostFunc &&post_func) {
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

        post_func();

        CHECK_MENU_OK(set_menu_items(m_menu, m_visible_items.data()));
        CHECK_MENU_OK(post_menu(m_menu));
        m_posted = true;

        CHECK_OK(refresh());
    }
    
    void reset() {
        update([this]() { this->post_all_items(); });
    }

#if USE_LEVENSHTEIN != 0
    struct CUSTOM_LEVENSHTEIN_COST_TABLE {
        static constexpr std::size_t deletion = 2u;
        static constexpr std::size_t insertion = 0u;
        static constexpr std::size_t substitution = 1u;
    };

    void edit(std::string_view curr_str) {
        std::vector<std::pair<std::size_t, ITEM*>> results;
        m_levenshtein_buffer.resize(curr_str.size() + 1);

        for (const auto &pair : m_manifest_manager.range()) {
            auto &back = results.emplace_back(0u, pair.first);
            
            if (!boost::ifind_first(pair.second, curr_str)) {
                back.first = levenshtein_distance<char, false, CUSTOM_LEVENSHTEIN_COST_TABLE>(curr_str, pair.second, m_levenshtein_buffer);
            }
        }

        std::sort(results.begin(), results.end(), 
                  [](const auto &lhs, const auto &rhs){ return lhs.first < rhs.first; });

        auto post = [&]() {
            std::swap(m_visible_items, m_items_back_buffer);

            m_visible_items.clear();
            m_visible_items.emplace_back(m_curr_item);

            for (const auto &pair : results) {
                m_visible_items.emplace_back(pair.second);
            }

            m_visible_items.emplace_back(nullptr);
        };
        update(post);
    }
#else
    void edit(std::string_view curr_str) {
        auto post = [&]() {
            std::swap(m_visible_items, m_items_back_buffer);

            m_visible_items.clear();
            m_visible_items.emplace_back(m_curr_item);

            for (const auto &pair : m_manifest_manager.range()) {
                if (boost::ifind_first(pair.second, curr_str)) {
                    m_visible_items.emplace_back(pair.first);
                }
            }

            m_visible_items.emplace_back(nullptr);
        };
        update(post);
    }
#endif

public:
    class result {
        std::string_view m_name;
        bool m_is_new;

    public:
        result(std::string_view name, bool is_new)
        :m_name{ name },
         m_is_new{ is_new }
        {}

        inline std::string_view name() const noexcept { return m_name; }
        inline bool is_new() const noexcept { return m_is_new; }
    };

    menu_manager(manifest_manager &manifest_manager)
    :m_manifest_manager{ manifest_manager }
    {
        m_char_buffer.reserve(1024);
        m_visible_items.reserve(100);
        m_items_back_buffer.reserve(100);
        m_levenshtein_buffer.reserve(1024);

        m_curr_item = new_item("<Current>", "");
        RUNTIME_ASSERT(m_curr_item != nullptr);

        post_all_items();

        m_menu = new_menu(m_visible_items.data());
        RUNTIME_ASSERT(m_menu != nullptr);

        CHECK_MENU_OK(set_menu_format(m_menu, LINES - 7, 1));

        status_bar_y = LINES - 2;
        sep2_y = LINES - 3;
        input_bar_y = LINES - 4;
        sep1_y = LINES - 5;
    }

    ~menu_manager() {
        if (m_posted) unpost_menu(m_menu);
        free_menu(m_menu);
        free_item(m_curr_item);
    }

    menu_manager(const menu_manager&) = delete;
    menu_manager &operator=(const menu_manager&) = delete;

    std::optional<result> next() {
        m_char_buffer.clear();
        reset();

        while (true) {
            int c = getch();

            switch(c) {
            case esc_char:
                return std::nullopt;

            case KEY_DOWN:
                menu_driver(m_menu, REQ_DOWN_ITEM);
                break;
            case KEY_UP:
                menu_driver(m_menu, REQ_UP_ITEM);
                break;
            case KEY_HOME:
                menu_driver(m_menu, REQ_FIRST_ITEM);
                break;
            case KEY_END:
                menu_driver(m_menu, REQ_LAST_ITEM);
                break;

            case int('\n'):
                {
                    ITEM* item = current_item(m_menu);

                    if (item == m_curr_item) {
                        if (!m_char_buffer.empty()) {
                            return result{ m_char_buffer, true };
                        }
                    }
                    else if (item != nullptr) {
                        const char* name = item_name(item);
                        RUNTIME_ASSERT(name != nullptr);
                        return result{ name, false };
                    }
                }
                break;

            case KEY_BACKSPACE:
                if (!m_char_buffer.empty()) {
                    m_char_buffer.pop_back();

                    if (m_char_buffer.empty()) {
                        reset();
                    }
                    else {
                        edit(m_char_buffer);
                    }
                }
                break;

            default:
                {
                    if (isalnum(c) || c == '_' || c == ' ') {
                        c = tolower(c);
                        m_char_buffer += c;
                        edit(m_char_buffer);
                    }
                }
            }
        }
    }

    void notify(const result &res, bool success) {
        if (success) {
            if (res.is_new()) {
                m_manifest_manager.add_name(res.name());
            }

            m_status_bar = "Successfully created directory \"";
            m_status_bar += res.name();
            m_status_bar += "\"";
        }
        else {
            m_status_bar = "Failed to create directory \"";
            m_status_bar += res.name();
            m_status_bar += "\"";
        }
    }

};

std::string_view strip(std::string_view str) {
    auto offset = str.find_first_not_of(" \t");
    if (offset != std::string_view::npos) {
        str = str.substr(offset);
    }

    offset = str.find_last_not_of(" \t/");
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
    std::vector<std::string_view> man;
    man.reserve(manifest_man.size());

    for (const auto [item, str] : manifest_man.range()) {
        man.emplace_back(str);
    }
    std::sort(man.begin(), man.end());

    std::ofstream fs{ filename.data(), std::ios_base::binary };
    RUNTIME_ASSERT(fs);

    for (const auto &name : man) {
        fs << name << "\n";
        RUNTIME_ASSERT(fs);
    }

    fs.flush();
    RUNTIME_ASSERT(fs);
}

bool create_directory(const std::string_view dirname) {
#if FAKE_CREATE_DIRECTORY == 0
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

    while (auto opt = menu_man.next()) {
        bool success = create_directory(opt->name());
        menu_man.notify(*opt, success);
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
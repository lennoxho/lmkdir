#include "lmkdir.hpp"
#include "levenshtein.hpp"

#define FAKE_CREATE_DIRECTORY 0
#define USE_LEVENSHTEIN 1

constexpr char const* const manifest_name = "lmkdir_manifest";
constexpr int esc_char = 27;
constexpr int del_char = 127;

namespace fs = std::filesystem;
using directory_manifest = std::vector<std::string>;

class manifest_manager {
    std::unordered_map<std::string, ITEM*> m_data;

public:
    manifest_manager(directory_manifest&& initial_names) {
        m_data.reserve(initial_names.size());

        for (auto &name : initial_names) {
            add_name(std::move(name));
        }

        initial_names.clear();
    }

    ~manifest_manager() {
        for (auto &pair : m_data) {
            free_item(pair.second);
        }
    }

    manifest_manager(const manifest_manager&) = delete;
    manifest_manager &operator=(const manifest_manager&) = delete;

    template <typename T>
    void add_name(T &&name) {
        auto [iter, is_new_name] = m_data.emplace(std::forward<T>(name), nullptr);
        if (is_new_name) {
            iter->second = new_item(iter->first.c_str(), "");
            RUNTIME_ASSERT(iter->second);
        }
    }

    void remove_name(const std::string &name) {
        auto iter = m_data.find(name);
        if (iter != m_data.end()) {
            free_item(iter->second);
            m_data.erase(iter);
        }
    }

    inline const auto &range() const noexcept {
        return m_data;
    }

    inline std::size_t size() const noexcept {
        return m_data.size();
    }
};

class result {
public:
    enum ACTION { CREATE, DELETE };
    
private:
    std::string_view m_name;
    ACTION m_action;

public:
    result(std::string_view name, ACTION action)
    :m_name{ name },
     m_action{ action }
    {}

    inline std::string_view name() const noexcept { return m_name; }
    inline ACTION action() const noexcept { return m_action; }
};

class menu_manager {
    std::vector<ITEM*> m_visible_items;
    std::vector<ITEM*> m_items_back_buffer;
    std::vector<std::int64_t> m_levenshtein_buffer;
    std::vector<std::byte> m_levenshtein_bitset;
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
        
        for (const auto &[str, item] : m_manifest_manager.range()) {
            m_visible_items.emplace_back(item);
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
    void edit(std::string_view curr_str) {
        std::vector<std::pair<std::int64_t, ITEM*>> results;
        m_levenshtein_buffer.resize(curr_str.size() + 1);
        m_levenshtein_bitset.resize((curr_str.size() + CHAR_BIT - 1) / CHAR_BIT);

        for (const auto &[str, item] : m_manifest_manager.range()) {
            auto &back = results.emplace_back(std::numeric_limits<std::int64_t>::max(), item);
            
            if (!boost::ifind_first(str, curr_str)) {
                back.first = modified_levenshtein_distance<char, false>(curr_str, str, m_levenshtein_buffer, m_levenshtein_bitset);
            }
        }

        std::sort(results.begin(), results.end(), 
                  [](const auto &lhs, const auto &rhs){ return lhs.first > rhs.first; });

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

            for (const auto &[str, item] : m_manifest_manager.range()) {
                if (boost::ifind_first(str, curr_str)) {
                    m_visible_items.emplace_back(item);
                }
            }

            m_visible_items.emplace_back(nullptr);
        };
        update(post);
    }
#endif

public:
    menu_manager(manifest_manager &manifest_manager)
    :m_manifest_manager{ manifest_manager }
    {
        m_char_buffer.reserve(1024);
        m_visible_items.reserve(100);
        m_items_back_buffer.reserve(100);
        m_levenshtein_buffer.reserve(1024);
        m_levenshtein_bitset.reserve(128);

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
                            return result{ m_char_buffer, result::CREATE };
                        }
                    }
                    else if (item != nullptr) {
                        const char* name = item_name(item);
                        RUNTIME_ASSERT(name != nullptr);
                        return result{ name, result::CREATE };
                    }
                }
                break;
                
            case KEY_DC:
                {
                    ITEM* item = current_item(m_menu);

                    if (item == m_curr_item) {
                        if (!m_char_buffer.empty()) {
                            return result{ m_char_buffer, result::DELETE };
                        }
                    }
                    else if (item != nullptr) {
                        const char* name = item_name(item);
                        RUNTIME_ASSERT(name != nullptr);
                        return result{ name, result::DELETE };
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
        if (res.action() == result::CREATE) {
            if (success) {
                m_manifest_manager.add_name(res.name());
    
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
        else if (res.action() == result::DELETE) {
            if (success) {
                m_manifest_manager.remove_name(std::string{ res.name() });
    
                m_status_bar = "Successfully deleted directory \"";
                m_status_bar += res.name();
                m_status_bar += "\"";
            }
            else {
                m_status_bar = "Failed to delete directory \"";
                m_status_bar += res.name();
                m_status_bar += "\"";
            }
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
        RUNTIME_MSG_ASSERT(fs, filename);

        const auto filesize = fs.tellg();
        contents.resize(gsl::narrow<std::size_t>(filesize));

        fs.seekg(0);
        RUNTIME_MSG_ASSERT(fs, filename);

        fs.read(contents.data(), filesize);
        RUNTIME_MSG_ASSERT(fs, filename);
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

bool create_directory(const std::string_view dirname) {
#if FAKE_CREATE_DIRECTORY == 0
    try {
        return fs::create_directory(dirname.data());
    }
    catch (const fs::filesystem_error &err) {
        return false;
    }
#endif
}

bool delete_directory(const std::string_view dirname) {
#if FAKE_CREATE_DIRECTORY == 0
    try {
        fs::remove_all(dirname.data());
    }
    catch (const fs::filesystem_error &err) {
        return false;
    }
#endif
    return true;
}

void write_directory_manifest(const std::string_view filename, const manifest_manager &manifest_man) {
    auto tmp_filename = std::string{ filename } + ".tmp";
    {
        std::vector<std::string_view> man;
        man.reserve(manifest_man.size());
    
        for (const auto &[str, item] : manifest_man.range()) {
            man.emplace_back(str);
        }
        std::sort(man.begin(), man.end());
    
        std::ofstream fs{ tmp_filename.data(), std::ios_base::binary };
        RUNTIME_MSG_ASSERT(fs, tmp_filename);
    
        for (const auto &name : man) {
            fs << name << "\n";
            RUNTIME_MSG_ASSERT(fs, tmp_filename);
        }
    
        fs.flush();
        RUNTIME_MSG_ASSERT(fs, tmp_filename);
    }
    
    std::error_code err;
    fs::rename(tmp_filename, filename, err);
    RUNTIME_MSG_ASSERT(!err, filename);
}

std::optional<std::string> get_real_executable_name() {
    char buff[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buff, PATH_MAX-1);
    if (len != -1) {
        buff[len] = '\0';
        return std::string{ buff };
    }

    return std::nullopt;
}

std::optional<std::string> get_manifest_filename(const std::string_view exe_name) {
    fs::path manifest_file{ manifest_name };
    if (fs::exists(manifest_file) && !fs::is_directory(manifest_file)) {
        return manifest_file.string();
    }
    
    manifest_file = exe_name;
    manifest_file.replace_filename(manifest_name);
    if (fs::exists(manifest_file) && !fs::is_directory(manifest_file)) {
        return manifest_file.string();
    }
    
    if (auto real_exe_name = get_real_executable_name()) {
        manifest_file = *real_exe_name;
        manifest_file.replace_filename(manifest_name);
        
        if (fs::exists(manifest_file) && !fs::is_directory(manifest_file)) {
            return manifest_file.string();
        }
    }
    
    return std::nullopt;
}

void lmkdir(const std::string_view exe_name) {
    struct screen_init_ {
        screen_init_() {
            initscr();
            CHECK_OK(cbreak());
            CHECK_OK(noecho());
            CHECK_OK(keypad(stdscr, TRUE));
        }
        ~screen_init_() { endwin(); }
    } screen_init;

    auto manifest_file = get_manifest_filename(exe_name);
    RUNTIME_ASSERT(manifest_file);

    manifest_manager manifest_man{ read_directory_manifest(*manifest_file) };
    menu_manager menu_man{ manifest_man };

    while (auto opt = menu_man.next()) {
        if (opt->action() == result::CREATE) {
            menu_man.notify(*opt, create_directory(opt->name()));
        }
        else if (opt->action() == result::DELETE) {
            menu_man.notify(*opt, delete_directory(opt->name()));
        }
    }

    write_directory_manifest(*manifest_file, manifest_man);
}

int main(int, char const* const* const argv) {
    try {
        lmkdir(argv[0]);
    }
    catch (const fatal_error &err) {
        std::cerr << "Error: " << err.what() << '\n';
        return err.error_code;
    }
    catch (const std::exception &err) {
        std::cerr << "Error: " << err.what() << '\n';
        return -1;
    }

    return 0;
}
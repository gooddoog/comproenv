#include <fstream>
#include <vector>
#include <experimental/filesystem>
#include <csignal>
#include <ctime>
#ifdef _WIN32
#include <Windows.h>
#undef min
#undef max
#pragma warning(disable: 4996)
#endif  // _WIN32
#if defined(__linux__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif
#include "environment.h"
#include "yaml_parser.h"
#include "shell.h"
#include "utils.h"

namespace comproenv {

namespace fs = std::experimental::filesystem;

void Shell::configure_commands_global() {
    add_command(State::GLOBAL, "se", "Set environment",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 2)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        for (size_t i = 0; i < envs.size(); ++i) {
            if (envs[i].get_name() == arg[1]) {
                current_env = (int)i;
                current_state = State::ENVIRONMENT;
                return 0;
            }
        }
        throw std::runtime_error("Incorrect environment name");
    });

    add_command(State::GLOBAL, "ce", "Create environment",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 2)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        for (size_t i = 0; i < envs.size(); ++i)
            if (envs[i].get_name() == arg[1])
                throw std::runtime_error("Environment named " + arg[1] + " already exists");
        envs.push_back(arg[1]);
        fs::path path = fs::path(env_prefix + arg[1]);
        if (!fs::exists(path)) {
            fs::create_directory(path);
        }
        if (global_settings["autosave"] == "on") {
            std::vector <std::string> save_args = {"s"};
            return commands[State::GLOBAL][save_args.front()](save_args);
        }
        return 0;
    });

    add_command(State::GLOBAL, "re", "Remove environment",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 2)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        for (size_t i = 0; i < envs.size(); ++i) {
            if (envs[i].get_name() == arg[1]) {
                envs.erase(envs.begin() + i);
                fs::path path = fs::path(env_prefix + arg[1]);
                if (global_settings["autosave"] == "on") {
                    std::vector <std::string> save_args = {"s"};
                    commands[State::GLOBAL][save_args.front()](save_args);
                }
                if (fs::exists(path)) {
                    return !fs::remove_all(path);
                }
            }
        }
        throw std::runtime_error("Incorrect environment name");
    });

    add_command(State::GLOBAL, "le", "List of environments",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        std::cout << "List of environments in global:\n";
        for (size_t i = 0; i < envs.size(); ++i) {
            std::cout << "|-> " << envs[i].get_name() << "\n";
            for (size_t j = 0; j < std::min(size_t(3), envs[i].get_tasks().size()); ++j) {
                std::cout << "    |-> " << envs[i].get_tasks()[j].get_name() << ": " <<
                    envs[i].get_tasks()[j].get_settings()["language"] << "\n";
            }
            if (envs[i].get_tasks().size() > 3) {
                std::cout << "    (and " << envs[i].get_tasks().size() - 3ul << " more...)" "\n";
            }
        }
        std::cout << std::flush;
        return 0;
    });

    add_command(State::GLOBAL, "s", "Save settings",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        int indent = 0;
        std::ofstream f(config_file, std::ios::out);

        auto serialize_mapping = [&](const std::vector <std::pair <std::string, std::string>> &instances,
                                    const std::string instances_name) -> void {
            if (instances.size()) {
                for (int i = 0; i < indent; ++i)
                    f << " ";
                f << instances_name << ":" << std::endl;
                indent += 2;
                for (auto &instance : instances) {
                    for (int i = 0; i < indent; ++i)
                        f << " ";
                    f << instance.first << ": " << instance.second << std::endl;
                }
                indent -= 2;
            }
        };

        auto serialize_sequence = [&](const std::vector <std::string> &instances,
                                    const std::string instances_name) -> void {
            if (instances.size()) {
                for (int i = 0; i < indent; ++i)
                    f << " ";
                f << instances_name << ":" << std::endl;
                indent += 2;
                for (auto &instance : instances) {
                    for (int i = 0; i < indent; ++i)
                        f << " ";
                    f << "- " << '\"' << instance << '\"' << std::endl;
                }
                indent -= 2;
            }
        };

        auto serialize_settings = [&](std::map <std::string, std::string> &settings) {
            std::vector <std::pair <std::string, std::string>> compilers, runners, templates, aliases;

            for (auto &setting : settings) {
                if (setting.first.compare(0, std::size("compiler_") - 1, "compiler_") == 0) {
                    compilers.emplace_back(setting.first.substr(std::size("compiler_") - 1), setting.second);
                } else if (setting.first.compare(0, std::size("runner_") - 1, "runner_") == 0) {
                    runners.emplace_back(setting.first.substr(std::size("runner_") - 1), setting.second);
                } else if (setting.first.compare(0, std::size("template_") - 1, "template_") == 0) {
                    templates.emplace_back(setting.first.substr(std::size("template_") - 1), setting.second);
                } else if (setting.first.compare(0, std::size("alias_") - 1, "alias_") == 0) {
                    aliases.emplace_back(setting.first.substr(std::size("alias_") - 1), setting.second);
                } else {
                    for (int i = 0; i < indent; ++i)
                        f << " ";
                    f << setting.first << ": " << setting.second << std::endl;
                }
            }
            serialize_mapping(compilers, "compilers");
            serialize_mapping(runners, "runners");
            serialize_mapping(templates, "templates");
            serialize_mapping(aliases, "aliases");
        };
        if (global_settings.size()) {
            f << "global:" << std::endl;
            indent += 2;
            serialize_settings(global_settings);
            serialize_sequence(commands_history.get_all(), "commands_history");
            indent -= 2;
        }
        f.close();
        f.open(environments_file, std::ios::out);
        if (envs.size()) {
            f << "environments:" << std::endl;
            indent += 2;
            for (auto &env : envs) {
                for (int i = 0; i < indent; ++i)
                    f << " ";
                f << "- name: " << env.get_name() << std::endl;
                indent += 2;
                serialize_settings(env.get_settings());
                if (env.get_tasks().size()) {
                    for (int i = 0; i < indent; ++i)
                        f << " ";
                    f << "tasks:" << std::endl;
                    indent += 2;
                    for (auto &task : env.get_tasks()) {
                        for (int i = 0; i < indent; ++i)
                            f << " ";
                        f << "- name: " << task.get_name() << std::endl;
                        indent += 2;
                        serialize_settings(task.get_settings());
                        indent -= 2;
                    }
                    indent -= 2;
                }
                indent -= 2;
            }
            indent -= 2;
            f << std::endl;
        }
        f.close();
        return 0;
    });

    add_command(State::GLOBAL, "py-shell", "Launch Python shell",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        DEBUG_LOG(get_setting_by_name("python_interpreter"));
        return system(get_setting_by_name("python_interpreter").c_str());
    });
    add_alias(State::GLOBAL, "py-shell", State::ENVIRONMENT, "py-shell");
    add_alias(State::GLOBAL, "py-shell", State::TASK, "py-shell");
    add_alias(State::GLOBAL, "py-shell", State::GENERATOR, "py-shell");

    add_command(State::GLOBAL, "autosave", "Toggle autosave",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        std::string state = "on";
        auto it = global_settings.find("autosave");
        if (it != global_settings.end()) {
            state = it->second;
            global_settings.erase(it);
        }
        if (state == "on")
            state = "off";
        else if (state == "off")
            state = "on";
        else
            throw std::runtime_error("Unknown state for autosave");
        global_settings["autosave"] = state;
        std::cout << "Set autosave to " << state << std::endl;
        return 0;
    });
    add_alias(State::GLOBAL, "autosave", State::ENVIRONMENT, "autosave");
    add_alias(State::GLOBAL, "autosave", State::TASK, "autosave");
    add_alias(State::GLOBAL, "autosave", State::GENERATOR, "autosave");

    add_command(State::GLOBAL, "history", "Show commands history",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        auto history = commands_history.get_all();
        std::cout << "Commands history:\n";
        for (const auto &command : history) {
            std::cout << command << '\n';
        }
        return 0;
    });
    add_alias(State::GLOBAL, "history", State::ENVIRONMENT, "history");
    add_alias(State::GLOBAL, "history", State::TASK, "history");
    add_alias(State::GLOBAL, "history", State::GENERATOR, "history");

    add_command(State::GLOBAL, "reload-settings", "Hot reload settings from config file ",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        YAMLParser config_parser(config_file);
        YAMLParser::Mapping config = config_parser.parse().get_mapping();
        YAMLParser environments_parser(config_file);
        YAMLParser::Mapping environments = environments_parser.parse().get_mapping();
        global_settings.clear();
        envs.clear();
        parse_settings(config, environments);
        configure_user_defined_aliases();
        create_paths();
        current_env = -1;
        current_task = -1;
        current_state = State::GLOBAL;
        return 0;
    });
    add_alias(State::GLOBAL, "reload-settings", State::ENVIRONMENT, "reload-settings");
    add_alias(State::GLOBAL, "reload-settings", State::TASK, "reload-settings");
    add_alias(State::GLOBAL, "reload-settings", State::GENERATOR, "reload-settings");

    add_command(State::GLOBAL, "reload-envs", "Reload all environments and tasks from comproenv directory",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        envs.clear();
        for (auto &p : fs::directory_iterator(".")) {
            std::string env_dir = p.path().filename().string();
            if (env_dir.find(env_prefix) == 0) {
                std::string env_name = env_dir.substr(env_prefix.size(), env_dir.size() - env_prefix.size());
                envs.push_back(Environment(env_name));
                for (auto &q : fs::directory_iterator(env_dir)) {
                    std::string task_dir = q.path().filename().string();
                    std::string task_name = task_dir.substr(task_prefix.size(), task_dir.size() - task_prefix.size());
                    if (task_dir.find(task_prefix) == 0) {
                        envs.back().get_tasks().push_back(Task(task_name));
                        for (auto &r : fs::directory_iterator(fs::path(env_dir) / fs::path(task_dir))) {
                            if (r.path().has_extension()) {
                                std::string task_lang = r.path().extension().string().substr(1);
                                if (task_lang != "in" && task_lang != "out" && task_lang != "exe") {
                                    envs.back().get_tasks().back().add_setting("language", task_lang);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        return 0;
    });
    add_alias(State::GLOBAL, "reload-envs", State::ENVIRONMENT, "reload-envs");
    add_alias(State::GLOBAL, "reload-envs", State::TASK, "reload-envs");
    add_alias(State::GLOBAL, "reload-envs", State::GENERATOR, "reload-envs");

    add_command(State::GLOBAL, "set", "Configure global settings",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() == 2) {
            global_settings.erase(arg[1]);
        } else if (arg.size() >= 3) {
            std::string second_arg = arg[2];
            for (unsigned i = 3; i < arg.size(); ++i) {
                second_arg.push_back(' ');
                second_arg += arg[i];
            }
            global_settings.erase(arg[1]);
            global_settings.emplace(arg[1], second_arg);
        } else {
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        }
        if (global_settings["autosave"] == "on") {
            std::vector <std::string> save_args = {"s"};
            return commands[State::GLOBAL][save_args.front()](save_args);
        }
        return 0;
    });

    add_command(State::GLOBAL, "sets", "Print settings",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() > 2)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        if (global_settings.size()) {
            std::cout << "Settings in " << state_names[State::GLOBAL] << ":\n";
            for (const auto &it : global_settings) {
                std::cout << "    \"" << it.first << "\" : \"" << it.second << "\"\n";
            }
        }
        if (current_env != -1 && envs[current_env].get_settings().size()) {
            std::cout << "Settings in " << state_names[State::ENVIRONMENT] << ":\n";
            for (const auto &it : envs[current_env].get_settings()) {
                std::cout << "    \"" << it.first << "\" : \"" << it.second << "\"\n";
            }
        }
        if (current_task != -1 && envs[current_task].get_tasks()[current_task].get_settings().size()) {
            std::cout << "Settings in " << state_names[State::TASK] << ":\n";
            for (const auto &it : envs[current_task].get_tasks()[current_task].get_settings()) {
                std::cout << "    \"" << it.first << "\" : \"" << it.second << "\"\n";
            }
        }
        return 0;
    });
    add_alias(State::GLOBAL, "sets", State::ENVIRONMENT, "sets");
    add_alias(State::GLOBAL, "sets", State::TASK, "sets");

    add_command(State::GLOBAL, "q", "Exit from program",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        std::cout << "Exiting..." << std::endl;
        if (global_settings["autosave"] == "on") {
            std::vector <std::string> save_args = {"s"};
            int res = commands[State::GLOBAL][save_args.front()](save_args);
            exit(res);
        } else {
            exit(0);
        }
    });
    add_alias(State::GLOBAL, "q", State::GLOBAL, "exit");

    add_command(State::GLOBAL, "alias", "Define aliases for commands",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 3)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        add_alias(current_state, arg[1], current_state, arg[2]);
        auto it = global_settings.find("alias_" + state_names[current_state]);
        if (it == global_settings.end()) {
            global_settings.emplace("alias_" + state_names[current_state], "");
            it = global_settings.find("alias_" + state_names[current_state]);
        }
        if (std::size(it->second) && it->second.back() != ' ')
            it->second.push_back(' ');
        it->second += arg[1] + ' ' + arg[2] + ' ';
        if (global_settings["autosave"] == "on") {
            std::vector <std::string> save_args = {"s"};
            return commands[State::GLOBAL][save_args.front()](save_args);
        }
        return 0;
    });
    add_alias(State::GLOBAL, "alias", State::ENVIRONMENT, "alias");
    add_alias(State::GLOBAL, "alias", State::TASK, "alias");
    add_alias(State::GLOBAL, "alias", State::GENERATOR, "alias");

    add_command(State::GLOBAL, "delete-alias", "Delete aliases for commands",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 2)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        auto it = global_settings.find("alias_" + state_names[current_state]);
        if (it == global_settings.end())
            throw std::runtime_error("Aliases in state " + state_names[current_state] + " are not found");
        std::vector <std::string> aliases;
        split(aliases, it->second);
        std::function <void(const std::string_view)> delete_aliases =
        [&](const std::string_view str) {
            for (size_t i = 0; i < aliases.size(); ) {
                if (aliases[i] == str) {
                    delete_aliases(aliases[i + 1]);
                    aliases.erase(aliases.begin() + i, aliases.begin() + i + 2);
                } else {
                    i += 2;
                }
            }
        };
        delete_aliases(arg[1]);
        it->second = join(" ", aliases);
        if (global_settings["autosave"] == "on") {
            std::vector <std::string> save_args = {"s"};
            return commands[State::GLOBAL][save_args.front()](save_args);
        }
        return 0;
    });
    add_alias(State::GLOBAL, "delete-alias", State::ENVIRONMENT, "delete-alias");
    add_alias(State::GLOBAL, "delete-alias", State::TASK, "delete-alias");
    add_alias(State::GLOBAL, "delete-alias", State::GENERATOR, "delete-alias");

    add_command(State::GLOBAL, "help", "Help",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        std::cout << "Help:" << "\n";
        size_t max_name_length = 0, max_desc_length = 0;
        for (auto &help_info : help[current_state]) {
            for (const std::string &command : help_info.second)
                max_name_length = std::max(max_name_length, command.size());
            max_desc_length = std::max(max_desc_length, help_info.first.size());
        }
        for (size_t i = 0; i < max_name_length + 1; ++i)
            std::cout << '-';
        std::cout << '|';
        for (size_t i = 0; i < max_desc_length + 1; ++i)
            std::cout << '-';
        std::cout << '\n';
        for (auto &help_info : help[current_state]) {
            // Line
            bool flag = false;
            for (const std::string &command : help_info.second) {
                for (size_t i = 0; i < max_name_length - command.size(); ++i)
                    std::cout << ' ';
                std::cout << command << " |";
                if (!flag) {
                    std::cout << ' ' << help_info.first << "\n";
                    flag = true;
                } else {
                    std::cout << "\n";
                }
            }
            // Separator
            for (size_t i = 0; i < max_name_length + 1; ++i)
                std::cout << '-';
            std::cout << '|';
            for (size_t i = 0; i < max_desc_length + 1; ++i)
                std::cout << '-';
            std::cout << '\n';
        }
        return 0;
    });
    add_alias(State::GLOBAL, "help", State::GLOBAL, "?");

    add_command(State::GLOBAL, "about", "Get information about comproenv executable and environment",
    [](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        std::cout << "Repo: https://github.com/gooddoog/comproenv.git\n";
        std::cout << "Commit: " TOSTRING(COMPROENV_HASH) "\n";
        std::cout << "3rd party dependencies:\n";
        std::cout << "    libyaml: " TOSTRING(COMPROENV_LIBYAML_HASH) "\n";
        std::cout << "Build   time: " TOSTRING(COMPROENV_BUILDTIME) "\n";
        std::cout << "Current time: ";
        time_t now = time(0);
        tm *ctm = localtime(&now);
        printf("%d-%02d-%02d %02d:%02d:%02d\n",
                1900 + ctm->tm_year, 1 + ctm->tm_mon, ctm->tm_mday,
                ctm->tm_hour, ctm->tm_min, ctm->tm_sec);
        std::cout << "OS: ";
        #ifdef _WIN32
        OSVERSIONINFOEX info;
        ZeroMemory(&info, sizeof(OSVERSIONINFOEX));
        info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
        GetVersionEx((LPOSVERSIONINFO)& info);
        std::cout << "Microsoft Windows " << info.dwMajorVersion << "." << info.dwMinorVersion << "\n";
        #elif defined(__linux__) || defined(__APPLE__)
        utsname buf;
        if (uname(&buf)) {
            std::cout << "undefined *nix\n";
        } else {
            std::cout << buf.sysname << ' ' << buf.nodename << ' ' << buf.release << ' ' << buf.version << ' ' << buf.machine << "\n";
        }
        #else
        std::cout << "unknown\n";
        #endif
        std::cout << "Compiler: ";
        #ifdef __clang__
        std::cout << "clang++ " TOSTRING(__clang_major__) "." TOSTRING(__clang_minor__) "." TOSTRING(__clang_patchlevel__) "\n";
        #elif _MSC_FULL_VER
        std::cout << "MSVC " TOSTRING(_MSC_FULL_VER) "\n";
        #elif __GNUC__
        std::cout << "g++ " TOSTRING(__GNUC__) "." TOSTRING(__GNUC_MINOR__) "." TOSTRING(__GNUC_PATCHLEVEL__) "\n";
        #else
        std::cout << "unknown\n";
        #endif
        return 0;
    });
}

}  // namespace comproenv
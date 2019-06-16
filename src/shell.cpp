#include <fstream>
#include <vector>
#include <experimental/filesystem>
#include <stdexcept>
#include <csignal>
#include "environment.h"
#include "yaml_parser.h"
#include "shell.h"
#include "utils.h"

namespace fs = std::experimental::filesystem;

Shell::Shell(const std::string &file) : config_file(file) {
    signal(SIGINT, SIG_IGN);
    #ifndef _WIN32
    signal(SIGTSTP, SIG_IGN);
    #endif  // _WIN32
    configure_commands();
    if (config_file != "") {
        YAMLParser p1(config_file);
        YAMLParser::Mapping p = p1.parse().get_mapping();
        parse_settings(p);
    } else {
        config_file = "config.yaml";
        if (fs::exists(config_file)) {
            YAMLParser p1(config_file);
            YAMLParser::Mapping p = p1.parse().get_mapping();
            parse_settings(p);
        }
    }
    if (global_settings.find("python_interpreter") == global_settings.end()) {
        global_settings.emplace("python_interpreter", "python");
    }
    if (global_settings.find("autosave") == global_settings.end()) {
        global_settings.emplace("autosave", "on");
    }
    create_paths();
    current_env = -1;
    current_task = -1;
    current_state = State::GLOBAL;
}

void Shell::add_command(int state, std::string name,
                        std::string help_info,
                        std::function<int(std::vector <std::string> &)> func) {
    commands[state].emplace(name, func);
    help[state][help_info].insert(name);
}

void Shell::add_alias(int old_state, std::string old_name, int new_state, std::string new_name) {
    auto old_command = commands[old_state].find(old_name);
    if (old_command == commands[old_state].end())
        throw std::runtime_error("Unable to add alias for " + old_name);
    commands[new_state].emplace(new_name, old_command->second);
    for (auto &help_info : help[old_state]) {
        if (help_info.second.find(old_name) != help_info.second.end()) {
            help[new_state][help_info.first].insert("new_name");
            break;
        }
    }
}

void Shell::configure_commands() {
    configure_commands_global();
    configure_commands_environment();
    configure_commands_task();
    configure_commands_generator();
}

void Shell::parse_settings(YAMLParser::Mapping &config) {
    auto deserialize_compilers = [&](std::unordered_map <std::string, std::string> &settings, YAMLParser::Mapping &map) {
        if (map.has_key("compilers")) {
            std::map <std::string, YAMLParser::Value> compilers = map.get_value("compilers").get_mapping().get_map();
            for (auto &compiler_data : compilers) {
                settings.emplace("compiler_" + compiler_data.first, compiler_data.second.get_string());
            }
        }
    };
    auto deserialize_runners = [&](std::unordered_map <std::string, std::string> &settings, YAMLParser::Mapping &map) {
        if (map.has_key("runners")) {
            std::map <std::string, YAMLParser::Value> runners = map.get_value("runners").get_mapping().get_map();
            for (auto &runner_data : runners) {
                settings.emplace("runner_" + runner_data.first, runner_data.second.get_string());
            }
        }
    };
    auto deserialize_templates = [&](std::unordered_map <std::string, std::string> &settings, YAMLParser::Mapping &map) {
        if (map.has_key("templates")) {
            std::map <std::string, YAMLParser::Value> templates = map.get_value("templates").get_mapping().get_map();
            for (auto &template_data : templates) {
                settings.emplace("template_" + template_data.first, template_data.second.get_string());
            }
        }
    };
    auto deserialize_rest_settings = [&](std::unordered_map <std::string, std::string> &settings, YAMLParser::Mapping &map) {
        for (auto &setting : map.get_map()) {
            if (setting.first != "name" &&
                setting.first != "tasks" &&
                setting.first != "compilers" &&
                setting.first != "runners" &&
                setting.first != "templates") {
                settings.emplace(setting.first, setting.second.get_string());
            }
        }
    };
    if (config.has_key("environments")) {
        std::vector <YAMLParser::Value> environments = config.get_value("environments").get_sequence();
        for (auto &env_data : environments) {
            YAMLParser::Mapping map = env_data.get_mapping();
            Environment env(map.get_value("name").get_string());
            std::cout << "env: " << map.get_value("name").get_string() << std::endl;
            if (map.has_key("tasks")) {
                std::vector <YAMLParser::Value> tasks = map.get_value("tasks").get_sequence();
                for (auto &task_data : tasks) {
                    YAMLParser::Mapping map = task_data.get_mapping();
                    Task task(map.get_value("name").get_string());
                    deserialize_compilers(task.get_settings(), map);
                    deserialize_runners(task.get_settings(), map);
                    deserialize_templates(task.get_settings(), map);
                    deserialize_rest_settings(task.get_settings(), map);
                    env.add_task(task);
                }
            }
            deserialize_compilers(env.get_settings(), map);
            deserialize_runners(env.get_settings(), map);
            deserialize_templates(env.get_settings(), map);
            deserialize_rest_settings(env.get_settings(), map);
            envs.push_back(env);
        }
    }
    if (config.has_key("global")) {
        YAMLParser::Mapping global_settings_map = config.get_value("global").get_mapping();
        deserialize_compilers(global_settings, global_settings_map);
        deserialize_runners(global_settings, global_settings_map);
        deserialize_templates(global_settings, global_settings_map);
        deserialize_rest_settings(global_settings, global_settings_map);
    }
}

void Shell::create_paths() {
    fs::path path = fs::current_path();
    for (auto &env : envs) {
        fs::path env_path = path / ("env_" + env.get_name());
        if (!fs::exists(env_path)) {
            fs::create_directory(env_path);
        }
        for (auto &task : env.get_tasks()) {
            fs::path task_path = env_path / ("task_" + task.get_name());
            if (!fs::exists(task_path)) {
                fs::create_directory(task_path);
            }
            if (!fs::exists(task_path / (task.get_name() + "." + task.get_settings()["language"]))) {
                std::ofstream f(task_path / (task.get_name() + "." + task.get_settings()["language"]), std::ios::out);
                f.close();
            }
            if (!fs::exists(task_path / "tests")) {
                fs::create_directory(task_path / "tests");
            }
        }
    }
}

void Shell::run() {
    std::string command;
    std::vector <std::string> args;
    while (true) {
        std::cout << ">";
        if (current_env != -1) {
            std::cout << "/" << envs[current_env].get_name();
        }
        if (current_task != -1) {
            std::cout << "/" << envs[current_env].get_tasks()[current_task].get_name();
        }
        if (current_state == State::GENERATOR) {
            std::cout << "/gen";
        }
        std::cout << " ";
        std::flush(std::cout);
        while (std::cin.eof()) {
            std::cin.clear();
            std::cin.ignore(32767, '\n');
        }
        std::getline(std::cin, command);
        split(args, command);
        std::cout << "DBG: ";
        for (auto &el : args)
            std::cout << el << " ";
        std::cout << std::endl;
        if (args.size() == 0)
            continue;
        if (commands[current_state].find(args[0]) == commands[current_state].end()) {
            std::cout << "Unknown command " << args[0] << std::endl;
        } else {
            try {
                int verdict = commands[current_state][args[0]](args);
                if (verdict) {
                    std::cout << "Command " << args[0] << " returned " << verdict << std::endl;
                }
            } catch(std::runtime_error &re) {
                std::cout << "Error: " << re.what() << std::endl;
            }
        }
    }
}

void Shell::configure_commands_global() {
    add_command(State::GLOBAL, "se", "Set environment",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 2)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        for (size_t i = 0; i < envs.size(); ++i) {
            if (envs[i].get_name() == arg[1]) {
                current_env = i;
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
        fs::path path = fs::current_path() / ("env_" + arg[1]);
        if (!fs::exists(path)) {
            fs::create_directory(path);
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
                fs::path path = fs::current_path() / ("env_" + arg[1]);
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
        auto serialize_settings = [&](std::unordered_map <std::string, std::string> &settings) {
            std::vector <std::pair <std::string, std::string>> compilers, runners, templates;
            auto export_instances = [&](const std::vector <std::pair <std::string, std::string>>& instances,
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
            for (auto &setting : settings) {
                if (setting.first.compare(0, std::size("compiler_") - 1, "compiler_") == 0) {
                    compilers.emplace_back(setting.first.substr(std::size("compiler_") - 1), setting.second);
                } else if (setting.first.compare(0, std::size("runner_") - 1, "runner_") == 0) {
                    runners.emplace_back(setting.first.substr(std::size("runner_") - 1), setting.second);
                } else if (setting.first.compare(0, std::size("template_") - 1, "template_") == 0) {
                    templates.emplace_back(setting.first.substr(std::size("template_") - 1), setting.second);
                } else {
                    for (int i = 0; i < indent; ++i)
                        f << " ";
                    f << setting.first << ": " << setting.second << std::endl;
                }
            }
            export_instances(compilers, "compilers");
            export_instances(runners, "runners");
            export_instances(templates, "templates");
        };
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
        if (global_settings.size()) {
            f << "global:" << std::endl;
            indent += 2;
            serialize_settings(global_settings);
            indent -= 2;
        }
        f.close();
        return 0;
    });

    add_command(State::GLOBAL, "py-shell", "Launch Python shell",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        return system(global_settings["python_interpreter"].c_str());
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
            state = (*it).second;
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

    add_command(State::GLOBAL, "reload-settings", "Hot reload settings from config file ",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        YAMLParser p1(config_file);
        YAMLParser::Mapping p = p1.parse().get_mapping();
        global_settings.clear();
        envs.clear();
        parse_settings(p);
        create_paths();
        current_env = -1;
        current_task = -1;
        current_state = State::GLOBAL;
        return 0;
    });
    add_alias(State::GLOBAL, "reload-settings", State::ENVIRONMENT, "reload-settings");
    add_alias(State::GLOBAL, "reload-settings", State::TASK, "reload-settings");
    add_alias(State::GLOBAL, "reload-settings", State::GENERATOR, "reload-settings");

    add_command(State::GLOBAL, "q", "Exit from program",
    [this](std::vector <std::string> &arg) -> int {
        if (arg.size() != 1)
            throw std::runtime_error("Incorrect arguments for command " + arg[0]);
        std::cout << "Exiting..." << std::endl;
        if (global_settings["autosave"] == "on") {
            std::vector <std::string> save_args = {"s"};
            int res = commands[State::GLOBAL][save_args.front()](save_args);
            (void)res;
        }
        exit(0);
        return 0;
    });
    add_alias(State::GLOBAL, "q", State::GLOBAL, "exit");

    
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
        // std::cout << max_name_length << " " << max_desc_length << std::endl;
        // exit(0);
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
            for (size_t i = 0; i < max_desc_length; ++i)
                std::cout << '-';
            std::cout << '\n';
        }
        std::cout << std::flush;
        return 0;
    });
    add_alias(State::GLOBAL, "help", State::GLOBAL, "?");
}

Shell::~Shell() {
    
}
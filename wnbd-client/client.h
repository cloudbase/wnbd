/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <string>
#include <vector>
#include <boost/program_options.hpp>

#include <windows.h>

typedef void (*GetOptionsFunc)(
    boost::program_options::positional_options_description &positonal_opts,
    boost::program_options::options_description &named_opts);
typedef DWORD (*ExecuteFunc)(const boost::program_options::variables_map &vm);

class Client {
public:
    struct Command {
        std::string name;
        std::vector<std::string> aliases;
        std::string description;

        ExecuteFunc execute = nullptr;
        GetOptionsFunc get_options = nullptr;

        Command(std::string _name,
            std::vector<std::string> _aliases,
            std::string _description,
            ExecuteFunc _execute,
            GetOptionsFunc _get_options = nullptr)
            : name(_name)
            , aliases(_aliases)
            , description(_description)
            , execute(_execute)
            , get_options(_get_options)
        {
            Client::commands.push_back(this);
        }
    };

    static std::vector<Command*> commands;
    static const size_t LINE_WIDTH = 80;
    static const size_t MIN_LCOLUMN_WIDTH = 20;

    DWORD execute(int argc, const char** argv);
    static Command* get_command(std::string name);

    static void get_common_options(boost::program_options::options_description& options);
};

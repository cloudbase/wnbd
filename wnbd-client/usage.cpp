/*
 * Copyright (c) 2019 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "client.h"
#include "usage.h"

#include <iostream>
#include <iomanip>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>

namespace po = boost::program_options;
using namespace std;

string fmt_text(size_t margin, size_t width, string text)
{
    string result;
    boost::trim(text);
    while (!text.empty())
    {
        // Don't apply padding to the first line.
        if (!result.empty()) {
            result += string(margin, ' ');
        }

        // The text size exceeds the line width so we're trying to find
        // the best place to break the line.
        size_t n = text.size();
        if (text.size() > width) {
            n = text.rfind(" ", width);
            if (n == string::npos) {
                n = text.find(" ");
            }
            if (n == string::npos) {
                n = text.length();
            }
        }
        // We'll preserve newlines.
        n = min(n, text.find("\n"));

        result += text.substr(0, n);
        text.erase(0, n);
        boost::trim(text);

        if (!text.empty()) {
            result += "\n";
        }
    }
    return result;
}

string fmt_pos_opt_short(std::string opt_name)
{
    ostringstream s;
    s << "<" << opt_name << ">";
    return s.str();
}

string fmt_opt_short(boost::shared_ptr<po::option_description> option)
{
    ostringstream s;
    bool required = option->semantic()->is_required();
    if (!required) {
        s << "[";
    }
    s << "--" << option->long_name();
    if (option->semantic()->max_tokens() != 0) {
        s << " <arg>";
    }
    if (!required) {
        s << "]";
    }
    return s.str();
}

void print_option_details(
    po::positional_options_description &positional_opts,
    po::options_description &named_opts,
    size_t& lcol_width,
    std::string optional_args_title = "Optional arguments")
{
    map<string, string> pos_opt_desc;

    // Right column margin.
    size_t option_indent = 2;
    lcol_width = max(lcol_width, Client::MIN_LCOLUMN_WIDTH);
    string last_pos_opt;
    for (unsigned int i = 0; i < positional_opts.max_total_count(); i++)
    {
        auto opt_name = positional_opts.name_for_position(i);
        if (opt_name == last_pos_opt) {
            break;
        }
        // Determine the margin and prepare a mapping with positional
        // option descriptions. The actual descriptions will be filled
        // when handling the named opts.
        pos_opt_desc.insert(pair<string, string>(opt_name, ""));
        lcol_width = max(lcol_width, opt_name.length() + 3 + option_indent);
    }

    size_t optional_arg_count = 0;
    for (auto named_opt : named_opts.options())
    {
        auto opt_name = named_opt->long_name();
        auto it = pos_opt_desc.find(opt_name);
        if (it != pos_opt_desc.end()) {
            it->second = named_opt->description();
            continue;
        }
        optional_arg_count += 1;
        lcol_width = max(lcol_width,
                     fmt_opt_short(named_opt).length() + 1 + option_indent);
    }

    if (!pos_opt_desc.empty()) {
        cout << "Positional arguments:" << endl;
        for (auto pos_opt : pos_opt_desc) {
            string formatted_desc = fmt_text(
                lcol_width,
                max(Client::LINE_WIDTH - lcol_width, Client::LINE_WIDTH / 3),
                pos_opt.second);
            cout << left << string(option_indent, ' ')
                 << setw(lcol_width - option_indent)
                 << fmt_pos_opt_short(pos_opt.first)
                 << formatted_desc << endl;
        }
        cout << endl;
    }

    if (optional_arg_count) {
        cout << optional_args_title << ":" << endl;
        for (auto named_opt : named_opts.options())
        {
            auto opt_name = named_opt->long_name();
            // Already printed.
            if (pos_opt_desc.find(opt_name) != pos_opt_desc.end()) {
                continue;
            }
            string formatted_desc = fmt_text(
                lcol_width,
                max(Client::LINE_WIDTH - lcol_width, Client::LINE_WIDTH / 3),
                named_opt->description());
            cout << left
                 << string(option_indent, ' ')
                 << setw(lcol_width - option_indent)
                 << fmt_opt_short(named_opt)
                 << formatted_desc << endl;
        }
        cout << endl;
    }
}

void print_command_usage(
    string binary_name,
    string command_name,
    po::positional_options_description &positional_opts,
    po::options_description &named_opts)
{
    string usage_str = "Usage: " + binary_name + " " + command_name;
    size_t margin = usage_str.length() + 1;

    set<string> pos_opt_names;
    ostringstream pos_stream;

    string last_pos_opt;
    for (unsigned int i = 0; i < positional_opts.max_total_count(); i++)
    {
        auto opt_name = positional_opts.name_for_position(i);
        if (opt_name == last_pos_opt) {
            pos_stream << " [<" << opt_name << "> ...]";
            break;
        }
        pos_stream << "<" << opt_name << "> ";
        pos_opt_names.insert(opt_name);
    }

    ostringstream ops_stream;
    for (auto named_opt : named_opts.options())
    {
        auto opt_name = named_opt->long_name();
        if (pos_opt_names.find(opt_name) != pos_opt_names.end()) {
            continue;
        }
        ops_stream << fmt_opt_short(named_opt) << " ";
    }

    string formatted_opts = fmt_text(
        margin,
        max(Client::LINE_WIDTH - margin - 1, Client::LINE_WIDTH / 3),
        boost::algorithm::join(vector<string>({pos_stream.str(), ops_stream.str()}), "\n"));

    cout << usage_str << " " << formatted_opts << endl;
}

DWORD print_command_help(string command_name)
{
    auto command = Client::get_command(command_name);
    if (!command) {
        cerr << "Unknown command: " << command_name << endl;
        return ERROR_INVALID_PARAMETER;
    }

    po::positional_options_description positional_opts;
    po::options_description named_opts;

    if (command->get_options) {
        command->get_options(positional_opts, named_opts);
    }

    print_command_usage("wnbd-client", command->name,
                        positional_opts, named_opts);

    cout << endl << fmt_text(0, Client::LINE_WIDTH, command->description)
         << endl << endl;

    // Use a consistent indentation for option groups.
    size_t lcol_width = Client::MIN_LCOLUMN_WIDTH;
    print_option_details(positional_opts, named_opts, lcol_width);

    // There aren't common positional arguments.
    po::positional_options_description common_pos_opts;
    po::options_description common_opts;
    Client::get_common_options(common_opts);
    print_option_details(common_pos_opts, common_opts, lcol_width,
                         "Common arguments");

    return 0;
}

void print_commands()
{
    cout << "wnbd-client commands: " << endl << endl;

    size_t name_col_width = 0;
    for (Client::Command *command : Client::commands)
    {
        size_t width = command->name.length();
        if (!command->aliases.empty()) {
            for (auto alias: command->aliases) {
                width += alias.length();
            }
        }
        width += (command->aliases.size() * 3);
        name_col_width = max(name_col_width, width);
    }
    for (Client::Command *command : Client::commands)
    {
        vector<string> cmd_names;
        cmd_names.push_back(command->name);
        cmd_names.insert(
            cmd_names.end(), command->aliases.begin(),
            command->aliases.end());
        string joined_cmd_names = boost::algorithm::join(cmd_names, " | ");
        string formatted_desc = fmt_text(
            name_col_width + 1,
            max(Client::LINE_WIDTH - name_col_width, Client::LINE_WIDTH / 3),
            command->description);
        cout << left << setw(name_col_width) << joined_cmd_names
             << " " << formatted_desc << endl;
    }
}

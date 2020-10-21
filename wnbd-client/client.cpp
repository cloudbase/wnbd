/*
 * Copyright (c) 2020 SUSE LLC
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "client.h"
#include "cmd.h"
#include "usage.h"
#include "wnbd.h"
#include "version.h"

#include <boost/exception/diagnostic_information.hpp>

#include <iostream>
#include <iomanip>

namespace po = boost::program_options;
using namespace std;

vector<Client::Command*> Client::commands;

Client::Command* Client::get_command(string name)
{
    for (Client::Command *command : Client::commands) {
        if (command->name == name)
            return command;
        for (auto alias: command->aliases) {
            if (alias == name)
                return command;
        }
    }

    return nullptr;
}

// Make sure to pass the EXACT SAME type that was used when defining the option,
// otherwise Boost will throw an exception.
template <class T>
T safe_get_param(const po::variables_map& vm, string name, T default_val = T())
{
    if (vm.count(name)) {
        return vm[name].as<T>();
    }
    return default_val;
}

void Client::get_common_options(po::options_description &options)
{
    options.add_options()
        ("debug", po::bool_switch(), "Enable debug logging.");
}

void handle_common_options(po::variables_map &vm)
{
    bool debug = safe_get_param<bool>(vm, "debug");

    WnbdLogLevel log_level = debug ? WnbdLogLevelDebug : WnbdLogLevelInfo;
    WnbdSetLogLevel(log_level);
}

DWORD Client::execute(int argc, const char** argv)
{
    vector<string> args;
    args.insert(args.end(), argv + 1, argv + argc);

    // The first must argument must be the command name.
    if (args.size() == 0) {
        args.push_back("help");
    }
    string command_name = args[0];

    Client::Command* command = get_command(command_name);
    if (!command) {
        cerr << "Unknown command: " << command_name << endl << endl
             << "See wnbd-client --help." << endl;
        return ERROR_INVALID_PARAMETER;
    }
    // Remove the command from the list of arguments.
    args.erase(args.begin());

    try {
        po::positional_options_description positional_opts;
        po::options_description named_opts;

        if (command->get_options) {
            command->get_options(positional_opts, named_opts);
        }
        get_common_options(named_opts);

        po::variables_map vm;
        po::store(po::command_line_parser(args)
            .options(named_opts)
            .positional(positional_opts)
            .run(), vm);
        po::notify(vm);

        handle_common_options(vm);
        return command->execute(vm);
    }
    catch (po::required_option& e) {
        cerr << "wnbd-client: " << e.what() << endl;
        return ERROR_INVALID_PARAMETER;
    }
    catch (po::too_many_positional_options_error&) {
        cerr << "wnbd-client: too many arguments" << endl;
        return ERROR_INVALID_PARAMETER;
    }
    catch (po::error& e) {
        cerr << "wnbd-client: " << e.what() << endl;
        return ERROR_INVALID_PARAMETER;
    }
    catch (...) {
        cerr << "wnbd-client: Caught unexpected exception." << endl << endl;
        cerr << boost::current_exception_diagnostic_information() << endl;
        return ERROR_INTERNAL_ERROR;
    }
}

DWORD execute_version(const po::variables_map &vm)
{
    return CmdVersion();
}

DWORD execute_help(const po::variables_map &vm)
{
    if (vm.count("command-name")) {
        string command_name = vm["command-name"].as<string>();
        return print_command_help(command_name);
    }
    print_commands();
    return 0;
}

void get_help_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    positonal_opts.add("command-name", 1);
    named_opts.add_options()
        ("command-name", po::value<string>(), "Command name.");
}

DWORD execute_list(const po::variables_map &vm)
{
    return CmdList();
}

void get_map_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    positonal_opts.add("instance-name", 1)
                  .add("hostname", 1);
    named_opts.add_options()
        ("instance-name", po::value<string>()->required(), "Disk identifier.")
        ("hostname", po::value<string>()->required(), "NBD server hostname.")
        ("port", po::value<DWORD>()->default_value(10809),
            "NBD server port number. Default: 10809.")
        ("export-name", po::value<string>(),
            "NBD export name, defaults to <instance-name>.")
        ("skip-handshake", po::bool_switch(),
            "Skip NBD handshake and jump right into the transmission phase. "
            "This is useful for minimal NBD servers. If set, the disk size "
            "and block size must be provided.")
        ("disk-size", po::value<UINT64>(),
            "The disk size. Ignored when using NBD handshake.")
        ("block-size", po::value<UINT32>(),
            "The block size. Ignored when using NBD handshake.")
        ("read-only", po::bool_switch(), "Enable disk read-only mode.");
}

DWORD execute_map(const po::variables_map& vm)
{
    return CmdMap(
        safe_get_param<string>(vm, "instance-name").c_str(),
        safe_get_param<string>(vm, "hostname").c_str(),
        safe_get_param<DWORD>(vm, "port"),
        safe_get_param<string>(vm, "export-name").c_str(),
        safe_get_param<UINT64>(vm, "disk-size"),
        safe_get_param<UINT32>(vm, "block-size"),
        safe_get_param<bool>(vm, "skip-handshake"),
        safe_get_param<bool>(vm, "read-only"));
}

void get_unmap_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    positonal_opts.add("instance-name", 1);
    named_opts.add_options()
        ("instance-name", po::value<string>()->required(), "Disk identifier.")
        ("hard-disconnect", po::bool_switch(),
            "Perform a hard disconnect. Warning: pending IO operations as "
            "well as unflushed data will be discarded.")
        ("no-hard-disconnect-fallback", po::bool_switch(),
            "Immediately return an error if the soft disconnect fails instead "
            "of attempting a hard disconnect as fallback.")
        ("soft-disconnect-timeout",
            po::value<DWORD>()->default_value(WNBD_DEFAULT_RM_TIMEOUT_MS / 1000),
            ("Soft disconnect timeout in seconds. The soft disconnect operation "
             "uses PnP to notify the Windows storage stack that the device is "
             "going to be disconnected. Storage drivers can block this operation "
             "if there are pending operations, unflushed caches or open handles. "
             "Default: " + to_string(WNBD_DEFAULT_RM_TIMEOUT_MS / 1000) + ".").c_str())
        ("soft-disconnect-retry-interval",
            po::value<DWORD>()->default_value(WNBD_DEFAULT_RM_RETRY_INTERVAL_MS / 1000),
            ("Soft disconnect retry interval in seconds. "
             "Default: " + to_string(WNBD_DEFAULT_RM_RETRY_INTERVAL_MS / 1000) + ".").c_str());
}

DWORD execute_unmap(const po::variables_map& vm)
{
    return CmdUnmap(
        safe_get_param<string>(vm, "instance-name").c_str(),
        safe_get_param<bool>(vm, "hard-disconnect"),
        safe_get_param<bool>(vm, "no-hard-disconnect-fallback"),
        safe_get_param<DWORD>(vm, "soft-disconnect-timeout"),
        safe_get_param<DWORD>(vm, "soft-disconnect-retry-interval"));
}

DWORD execute_stats(const po::variables_map& vm)
{
    return CmdStats(
        safe_get_param<string>(vm, "instance-name").c_str());
}

void get_stats_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    positonal_opts.add("instance-name", 1);
    named_opts.add_options()
        ("instance-name", po::value<string>()->required(), "Disk identifier.");
}

void get_list_opt_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    named_opts.add_options()
        ("persistent", po::bool_switch(), "List persistent options only.");
}

DWORD execute_list_opt(const po::variables_map& vm)
{
    return CmdListOpt(
        safe_get_param<bool>(vm, "persistent"));
}

void get_opt_getter_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    positonal_opts.add("name", 1);
    named_opts.add_options()
        ("name", po::value<string>()->required(), "Option name.")
        ("persistent", po::bool_switch(), "Get the persistent value, if set.");
}

DWORD execute_get_opt(const po::variables_map& vm)
{
    return CmdGetOpt(
        safe_get_param<string>(vm, "name").c_str(),
        safe_get_param<bool>(vm, "persistent"));
}

void get_opt_setter_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    positonal_opts.add("name", 1)
                  .add("value", 1);
    named_opts.add_options()
        ("name", po::value<string>()->required(), "Option name.")
        ("value", po::value<string>()->required(), "Option value.")
        ("persistent", po::bool_switch(), "Persist the value across reboots.");
}

DWORD execute_set_opt(const po::variables_map& vm)
{
    return CmdSetOpt(
        safe_get_param<string>(vm, "name").c_str(),
        safe_get_param<string>(vm, "value").c_str(),
        safe_get_param<bool>(vm, "persistent"));
}

void get_reset_opt_args(
    po::positional_options_description &positonal_opts,
    po::options_description &named_opts)
{
    positonal_opts.add("name", 1);
    named_opts.add_options()
        ("name", po::value<string>()->required(), "Option name.")
        ("persistent", po::bool_switch(), "Persist the value across reboots.");
}

DWORD execute_reset_opt(const po::variables_map& vm)
{
    return CmdResetOpt(
        safe_get_param<string>(vm, "name").c_str(),
        safe_get_param<bool>(vm, "persistent"));
}

Client::Command commands[] = {
    Client::Command(
        "version", {"-v"}, "Get the client, library and driver version.",
        execute_version),
    Client::Command(
        "help", {"-h", "--help"},
        "List all commands or get more details about a specific command.",
        execute_help, get_help_args),
    Client::Command(
        "list", {"ls"}, "List WNBD disks",
        execute_list),
    Client::Command(
        "map", {}, "Create a new disk mapping, connecting to the "
                   "specified NBD server.",
        execute_map, get_map_args),
    Client::Command(
        "unmap", {"rm"}, "Remove disk mapping.",
        execute_unmap, get_unmap_args),
    Client::Command(
        "stats", {}, "Get disk stats.",
        execute_stats, get_stats_args),
    Client::Command(
        "list-opt", {}, "List driver options.",
        execute_list_opt, get_list_opt_args),
    Client::Command(
        "get-opt", {}, "Get driver option.",
        execute_get_opt, get_opt_getter_args),
    Client::Command(
        "set-opt", {}, "Set driver option.",
        execute_set_opt, get_opt_setter_args),
    Client::Command(
        "reset-opt", {}, "Reset driver option.",
        execute_reset_opt, get_reset_opt_args),
};

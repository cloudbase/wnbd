/*
 * Copyright (C) 2023 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#include "pch.h"

#include <vector>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>

namespace po = boost::program_options;
using namespace std;

po::variables_map Vm;

void AddCommonOptions(
    po::positional_options_description &PosOpts,
    po::options_description &NamedOpts)
{
    NamedOpts.add_options()
        ("log-level", po::value<DWORD>()->default_value(WnbdLogLevelWarning),
            "Wnbd log level.")
        ("help", po::bool_switch(), "Show usage.");
}

void AddNbdOptions(
    po::positional_options_description &PosOpts,
    po::options_description &NamedOpts)
{
    NamedOpts.add_options()
        ("nbd-export-name", po::value<string>(),
            "NBD export name")
        ("nbd-hostname", po::value<string>(),
            "NBD server hostname.")
        ("nbd-port", po::value<DWORD>()->default_value(10809),
            "NBD server port number. Default: 10809.");
}

void ParseOptions(int Argc, char** Argv)
{
    po::positional_options_description PosOpts;
    po::options_description NamedOpts;

    vector<string> Args;
    Args.insert(Args.end(), Argv + 1, Argv + Argc);

    try {
        AddCommonOptions(PosOpts, NamedOpts);
        AddNbdOptions(PosOpts, NamedOpts);

        po::store(po::command_line_parser(Args)
            .options(NamedOpts)
            .positional(PosOpts)
            .run(), Vm);
        po::notify(Vm);
    }
    catch (po::required_option& e) {
        cerr << "libwnbd_tests: " << e.what() << endl;
        exit(-1);
    }
    catch (po::too_many_positional_options_error&) {
        cerr << "libwnbd_tests: too many arguments" << endl;
        exit(-1);
    }
    catch (po::error& e) {
        cerr << "libwnbd_tests: " << e.what() << endl;
        exit(-1);
    }
    catch (...) {
        cerr << "libwnbd_tests: Caught unexpected exception." << endl << endl;
        cerr << boost::current_exception_diagnostic_information() << endl;
        exit(-1);
    }
}

// TODO: consider using the helpers from usage.cpp if we ever end up having
// an internal library reused between libwnbd, wnbd-client and the tests.
void PrintHelp()
{
    char* Msg = R"HELP(
Wnbd test parameters:
  --log-level=[LOG_LEVEL]
      Wnbd log level

  --nbd-export-name=[NBD_EXPORT_NAME]
      Existing NBD export to be used for NBD tests.
      If unspecified, NBD tests will be skipped.
  --nbd-hostname=[NBD_HOSTNAME]
      The NBD server address.
  --nbd-port=[NBD_PORT]
      The NBD server port. Default: 10809.
)HELP";

    cout << Msg << endl;
}

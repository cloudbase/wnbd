/*
 * Copyright (C) 2023 Cloudbase Solutions
 *
 * Licensed under LGPL-2.1 (see LICENSE)
 */

#pragma once

#include <string>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

extern po::variables_map Vm;

// Make sure to pass the EXACT SAME type that was used when defining the option,
// otherwise Boost will throw an exception.
template <class T>
T GetOpt(std::string Name, T DefaultVal = T())
{
    if (Vm.count(Name)) {
        return Vm[Name].as<T>();
    }
    return DefaultVal;
}

void ParseOptions(int Argc, char** Argv);
void PrintHelp();

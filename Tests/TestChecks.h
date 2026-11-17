#pragma once

#include <cstdlib>
#include <iostream>
#include <source_location>

namespace TestChecks
{
inline const char* CurrentTest = "startup";

inline void Check(bool Passed, const char* Expression, std::source_location Location = std::source_location::current())
{
    if (!Passed)
    {
        std::cerr << CurrentTest << " failed at " << Location.file_name() << '(' << Location.line()
                  << "): " << Expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

#define CHECK(Expression) TestChecks::Check(static_cast<bool>(Expression), #Expression)

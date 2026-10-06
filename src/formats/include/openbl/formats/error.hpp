#pragma once

#include <stdexcept>

namespace openbl::formats {

// Thrown when a game file is malformed or does not match the expected layout.
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace openbl::formats

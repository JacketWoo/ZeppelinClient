#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>

namespace utils {

void GetSeparateArgs(const std::string& raw_str, std::vector<std::string>* argv_p);

}

#endif

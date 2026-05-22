#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct TlsfInfo {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::string ltl;
    bool mealy_semantics;
};

TlsfInfo extract_tlsf_info(const std::string& tlsfFilePath);
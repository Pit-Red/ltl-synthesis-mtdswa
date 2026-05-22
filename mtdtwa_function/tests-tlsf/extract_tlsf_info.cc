#include "extract_tlsf_info.hh"
#include <iostream>



static std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string run_command(const std::string& cmd) {
    std::array<char, 256> buffer{};
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to start command: " + cmd);
    }

    try {
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            result += buffer.data();
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }

    const int rc = pclose(pipe);
    if (rc != 0) {
        throw std::runtime_error("Command failed: " + cmd);
    }

    return result;
}

static std::vector<std::string> parse_signal_list(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream iss(text);
    std::string token;

    while (iss >> token) {
        token = trim(token);

        while (!token.empty() && (token.back() == ',' || token.back() == ';')) {
            token.pop_back();
        }

        token = trim(token);
        if (!token.empty()) {
            result.push_back(token);
        }
    }

    return result;
}

TlsfInfo extract_tlsf_info(const std::string& tlsfFilePath) {
    TlsfInfo info;

    info.inputs  = parse_signal_list(run_command("syfco -ins \"" + tlsfFilePath + "\""));
    info.outputs = parse_signal_list(run_command("syfco -outs \"" + tlsfFilePath + "\""));
    info.ltl     = trim(run_command("syfco -f ltl \"" + tlsfFilePath + "\""));

    std::string semantics = trim(run_command("syfco -s \"" + tlsfFilePath + "\""));
    info.mealy_semantics = (semantics.compare(std::string("Mealy")) == 0);

    return info;
}

// int main(int argc, char** argv) {
//     try {
//         if (argc != 2) {
//             std::cerr << "Usage: " << argv[0] << " <path_to_tlsf_file>\n";
//             return 1;
//         }

//         const std::string path = argv[1];
//         TlsfInfo info = extract_tlsf_info(path);

//         std::cout << "INPUTS:\n";
//         for (const auto& s : info.inputs) {
//             std::cout << "  - " << s << '\n';
//         }

//         std::cout << "\nOUTPUTS:\n";
//         for (const auto& s : info.outputs) {
//             std::cout << "  - " << s << '\n';
//         }

//         std::cout << "\nLTL:\n" << info.ltl << '\n';

//         std::cout << "\nSemantics: " << (info.mealy_semantics ? "Mealy" : "Moore") << '\n';
//     } catch (const std::exception& e) {
//         std::cerr << "Error: " << e.what() << '\n';
//         return 1;
//     }

//     return 0;
// }
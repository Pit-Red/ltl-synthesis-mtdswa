#include "extract_tlsf_info.hh"
#include <iostream>
#include <sys/wait.h>
#include <cctype>
#include <unordered_set>



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
    info.semantics = trim(run_command("syfco -s \"" + tlsfFilePath + "\""));
    info.mealy_semantics = info.semantics.find("Mealy") != std::string::npos;
    if (!info.mealy_semantics && info.semantics.find("Moore") == std::string::npos)
        throw std::runtime_error("Unknown semantics from syfco: " + info.semantics);

    // -m fully: fully parenthesized, so the result does not depend on syfco
    //   and Spot agreeing on operator precedence.
    // -os mealy|moore: syfco only encodes Strict semantics into the formula
    //   (ASSERT W !REQUIRE instead of G REQUIRE -> G ASSERT) when asked to
    //   convert a Strict spec to the non-strict variant; plain `-f ltl`
    //   silently drops strictness.  For non-Strict files this is a no-op.
    const std::string os = info.mealy_semantics ? "mealy" : "moore";
    info.ltl = trim(run_command("syfco -f ltl -m fully -os " + os +
                                " \"" + tlsfFilePath + "\""));

    return info;
}

const char* to_string(LtlsyntVerdict v) {
    switch (v) {
        case LtlsyntVerdict::Realizable:   return "REALIZABLE";
        case LtlsyntVerdict::Unrealizable: return "UNREALIZABLE";
        case LtlsyntVerdict::Timeout:      return "TIMEOUT";
        case LtlsyntVerdict::Error:        return "ERROR";
    }
    return "ERROR";
}

static std::string join_comma(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i)
        out += (i ? "," : "") + v[i];
    return out;
}

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<std::string> lower_all(const std::vector<std::string>& v) {
    std::vector<std::string> out;
    for (const auto& x : v) out.push_back(lower(x));
    return out;
}

LtlsyntTlsfResult ltlsynt_tlsf_realizability(const std::string& tlsfFilePath,
                                             const TlsfInfo& info,
                                             unsigned timeout_seconds) {
    LtlsyntTlsfResult res;

    // `-os mealy` converts Moore to Mealy (each input i becomes X i) and
    // encodes Strict semantics into the formula (see extract_tlsf_info).
    const std::string os = "mealy";

    // Same format ltlsynt --tlsf uses internally: `-f ltlxba -m fully`.
    // It lowercases all signal names.  This matters because ltlsynt
    // lowercases the names given to --ins/--outs but NOT the names in the
    // formula, so an uppercase AP would silently match neither list.
    const std::string syfco_cmd = "syfco -f ltlxba -m fully -os " + os +
                                  " \"" + tlsfFilePath + "\"";
    try {
        res.mealy_formula = trim(run_command(syfco_cmd));
    } catch (const std::exception& e) {
        res.raw_output = e.what();
        return res;
    }

    const std::vector<std::string> ins = lower_all(info.inputs);
    const std::vector<std::string> outs = lower_all(info.outputs);
    {
        std::unordered_set<std::string> seen(ins.begin(), ins.end());
        seen.insert(outs.begin(), outs.end());
        if (seen.size() != ins.size() + outs.size()) {
            res.raw_output = "signal names collide after lowercasing";
            return res;
        }
    }

    // Give both lists: ltlsynt then errors out on any unclassified AP
    // instead of guessing its owner.
    const std::string cmd = syfco_cmd + " | timeout " + std::to_string(timeout_seconds) +
                            " ltlsynt -F - --ins=" + join_comma(ins) +
                            " --outs=" + join_comma(outs) + " --realizability 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        res.raw_output = "failed to start: " + cmd;
        return res;
    }
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        res.raw_output += buffer.data();
    const int status = pclose(pipe);
    const int rc = (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;

    // ltlsynt exit codes: 0 = realizable, 1 = unrealizable, 2 = error;
    // `timeout` exits with 124 when the time limit is hit.
    const std::string out = trim(res.raw_output);
    if (rc == 124)
        res.verdict = LtlsyntVerdict::Timeout;
    else if (rc == 0 && out == "REALIZABLE")
        res.verdict = LtlsyntVerdict::Realizable;
    else if (rc == 1 && out == "UNREALIZABLE")
        res.verdict = LtlsyntVerdict::Unrealizable;
    else
        res.verdict = LtlsyntVerdict::Error;
    return res;
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
#include "extract_tlsf_info.hh"
#include "../solve_el_aut_swa.hh"

#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>
#include <exception>
#include <algorithm>

namespace fs = std::filesystem;

struct CsvRow {
    std::string file_name;
    std::string full_path;
    std::string semantics;
    size_t num_inputs = 0;
    size_t num_outputs = 0;
    size_t num_states = 0;
    bool deterministic = false;
    bool solver_ran = false;
    size_t winning_states_count = 0;
    std::string mtdswa;    // "realizable" / "unrealizable" / "" if not run
    std::string ltlsynt;   // REALIZABLE / UNREALIZABLE / TIMEOUT / ERROR / "" if not run
    std::string result;    // MATCH / MISMATCH / INCONCLUSIVE
    std::string error;
};

static std::string csv_escape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

static void write_csv_header(std::ofstream& csv) {
    csv << "file_name,full_path,semantics,num_inputs,num_outputs,num_states,deterministic,solver_ran,winning_states_count,mtdswa,ltlsynt,result,error\n";
}

static void write_csv_row(std::ofstream& csv, const CsvRow& row) {
    csv
        << csv_escape(row.file_name) << ","
        << csv_escape(row.full_path) << ","
        << csv_escape(row.semantics) << ","
        << row.num_inputs << ","
        << row.num_outputs << ","
        << row.num_states << ","
        << (row.deterministic ? "true" : "false") << ","
        << (row.solver_ran ? "true" : "false") << ","
        << row.winning_states_count << ","
        << row.mtdswa << ","
        << row.ltlsynt << ","
        << row.result << ","
        << csv_escape(row.error) << "\n";
}

static spot::formula parse_ltl_checked(const std::string& ltl) {
    auto pf = spot::parse_infix_psl(ltl);
    if (pf.format_errors(std::cerr)) {
        throw std::runtime_error("LTL parse error");
    }
    return pf.f;
}

static spot::twa_graph_ptr build_automaton(const TlsfInfo& info) {
    spot::bdd_dict_preorder dict;
    std::unordered_set<spot::formula> come_first;

    spot::formula f = parse_ltl_checked(info.ltl);

    if (info.mealy_semantics) {
        for (const std::string& s : info.inputs)
            come_first.insert(spot::formula::ap(s));
    } else {
        for (const std::string& s : info.outputs)
            come_first.insert(spot::formula::ap(s));
    }

    f.traverse([&](const spot::formula& subf) {
        if (subf.is(spot::op::ap) && come_first.find(subf) != come_first.end()) {
            dict.register_proposition(subf);
        }
        return false;
    });

    spot::translator trans(dict);
    trans.set_type(spot::postprocessor::Generic);
    trans.set_pref(spot::postprocessor::Deterministic |
                   spot::postprocessor::Complete);

    return trans.run(f);
}

static CsvRow process_tlsf_file(const fs::path& path, unsigned ltlsynt_timeout) {
    CsvRow row;
    row.file_name = path.filename().string();
    row.full_path = path.string();
    row.result = "INCONCLUSIVE";

    try {
        TlsfInfo info = extract_tlsf_info(path.string());

        row.semantics = info.semantics;
        row.num_inputs = info.inputs.size();
        row.num_outputs = info.outputs.size();

        spot::twa_graph_ptr aut = build_automaton(info);

        row.num_states = aut->num_states();
        row.deterministic = aut->prop_universal().is_true();

        std::cout << "  " << row.semantics
                  << " | states=" << row.num_states
                  << " | deterministic=" << row.deterministic;

        if (!row.deterministic)
            throw std::runtime_error("automaton is not deterministic");

        // MTDTWA (SolveAutEL) is not used: it has a known bug.
        // The initial state is root 0, so realizable iff 0 is winning.
        row.solver_ran = true;
        StateSet win = SolveAutEL_swa(aut, info.outputs);
        row.winning_states_count = win.size();
        const bool swa_realizable = win.count(0) > 0;
        row.mtdswa = swa_realizable ? "realizable" : "unrealizable";
        std::cout << " | mtdswa=" << row.mtdswa;

        LtlsyntTlsfResult ref = ltlsynt_tlsf_realizability(path.string(), info, ltlsynt_timeout);
        row.ltlsynt = to_string(ref.verdict);
        std::cout << " | ltlsynt=" << row.ltlsynt;
        if (ref.verdict == LtlsyntVerdict::Realizable || ref.verdict == LtlsyntVerdict::Unrealizable) {
            const bool ref_realizable = ref.verdict == LtlsyntVerdict::Realizable;
            row.result = (ref_realizable == swa_realizable) ? "MATCH" : "MISMATCH";
        }
        std::cout << " | " << row.result << "\n";
    } catch (const std::exception& e) {
        row.error = e.what();
        std::cout << " | ERROR: " << row.error << "\n";
    } catch (...) {
        row.error = "unknown error";
        std::cout << " | ERROR: unknown\n";
    }

    return row;
}

int main(int argc, char const* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <path_to_tlsf_directory> [ltlsynt_timeout_seconds=60]\n"
                  << "Note: no per-file time limit for MTDSWA itself; for large\n"
                  << "suites, run tlsf_runner per file under `timeout` instead.\n";
        return 1;
    }
    const unsigned ltlsynt_timeout = argc == 3 ? std::stoul(argv[2]) : 60;

    fs::path input_path(argv[1]);

    if (!fs::exists(input_path)) {
        std::cerr << "Error: path does not exist: " << input_path << "\n";
        return 1;
    }

    if (!fs::is_directory(input_path)) {
        std::cerr << "Error: path is not a directory: " << input_path << "\n";
        return 1;
    }

    std::ofstream csv("results.csv");
    if (!csv) {
        std::cerr << "Error: cannot open results.csv for writing\n";
        return 1;
    }

    write_csv_header(csv);

    size_t total_files = 0;
    size_t deterministic_files = 0;
    size_t solver_runs = 0;
    size_t matches = 0, mismatches = 0, inconclusive = 0;

    // Recursive, sorted, so that nested benchmark families are included
    // and the order is reproducible.
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(input_path)) {
        if (!entry.is_regular_file())
            continue;

        if (entry.path().extension() != ".tlsf")
            continue;

        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const fs::path& file : files) {
        ++total_files;
        std::cout << "[" << total_files << "/" << files.size() << "] "
                  << file.string() << "\n" << std::flush;

        CsvRow row = process_tlsf_file(file, ltlsynt_timeout);
        if (row.result == "MATCH") ++matches;
        else if (row.result == "MISMATCH") ++mismatches;
        else ++inconclusive;

        if (row.deterministic)
            ++deterministic_files;
        if (row.solver_ran)
            ++solver_runs;

        write_csv_row(csv, row);
    }

    std::cout << "\nDone.\n";
    std::cout << "Total .tlsf files: " << total_files << "\n";
    std::cout << "Deterministic automata: " << deterministic_files << "\n";
    std::cout << "Solver runs: " << solver_runs << "\n";
    std::cout << "MATCH: " << matches << "  MISMATCH: " << mismatches
              << "  INCONCLUSIVE: " << inconclusive << "\n";
    std::cout << "CSV saved to results.csv\n";

    return mismatches > 0 ? 1 : 0;
}
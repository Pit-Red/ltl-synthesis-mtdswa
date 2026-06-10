#include "ltlgen.hh"
#include "ltlsynt.hh"
#include "../solve_el_aut.hh"
#include "../solve_el_aut_swa.hh"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/tl/parse.hh>

spot::twa_graph_ptr generate_automata(
    const std::string& formula,
    const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs,
    bool mealy_semantics=true
) {
    spot::bdd_dict_preorder dict;
    std::unordered_set<spot::formula> come_first;

    if (mealy_semantics)
        for (const std::string& s: inputs)
            come_first.insert(spot::formula::ap(s));
    else
        for (const std::string& s: outputs)
            come_first.insert(spot::formula::ap(s));

    spot::formula f = spot::parse_formula(formula);

    f.traverse([&](const spot::formula& f) {
        if (f.is(spot::op::ap) && (come_first.find(f) != come_first.end()))
            dict.register_proposition(f);
        return false;
    });

    spot::translator trans(dict);
    trans.set_type(spot::postprocessor::Generic);
    trans.set_pref(spot::postprocessor::Deterministic | spot::postprocessor::Complete);
    return trans.run(f);
}

struct BenchResult {
    std::string formula;
    unsigned num_states = 0;
    std::string acceptance;
    bool ltlsynt_realizable = false;
    bool swa_realizable = false;
    double ltlsynt_ms = -1;
    double swa_ms = -1;
    bool match = true;
    bool error = false;
    std::string error_msg;
};

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (std::getline(iss, tok, ','))
        tokens.push_back(tok);
    return tokens;
}

int main(int argc, char const *argv[])
{
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <inputs> <outputs> <seed> <n_samples> [tree_size] [csv_output]\n"
                  << "\n"
                  << "  <inputs>      Comma-separated input APs   (e.g. r1,r2)\n"
                  << "  <outputs>     Comma-separated output APs  (e.g. g1,g2)\n"
                  << "  <seed>        Random seed for formula generation\n"
                  << "  <n_samples>   Number of random formulas to test\n"
                  << "  [tree_size]   Formula tree size (default: 100)\n"
                  << "  [csv_output]  Path for CSV results file (default: bench_results.csv)\n";
        return 1;
    }

    std::string ltl_input = argv[1];
    std::string ltlsynt_output = argv[2];
    int random_seed = std::stoi(argv[3]);
    int number_of_samples = std::stoi(argv[4]);
    std::string tree_size = (argc > 5) ? argv[5] : "100";
    std::string csv_path = (argc > 6) ? argv[6] : "bench_results.csv";

    std::vector<std::string> atomic_props_input = split_csv(ltl_input);
    std::vector<std::string> atomic_props_output = split_csv(ltlsynt_output);

    std::vector<std::string> atomic_props_all = atomic_props_input;
    atomic_props_all.insert(
        atomic_props_all.end(),
        atomic_props_output.begin(),
        atomic_props_output.end()
    );

    RandLtlOptions gen_opts;
    gen_opts.atomic_props = atomic_props_all;
    gen_opts.seed = random_seed;
    gen_opts.formulas = number_of_samples;
    gen_opts.tree_size = tree_size;
    gen_opts.syntax = RandLtlOptions::Syntax::Spot;
    gen_opts.full_parentheses = false;

    std::vector<std::string> formulas = generate_random_ltl(gen_opts);

    LtlSyntOptions synt_opts;
    synt_opts.inputs = atomic_props_input;
    synt_opts.outputs = atomic_props_output;
    synt_opts.realizability_only = true;
    synt_opts.algo = "acd";
    synt_opts.simplify = "no";
    synt_opts.decompose = "yes";
    synt_opts.bypass = "yes";

    std::vector<BenchResult> results;
    results.reserve(formulas.size());

    int mismatches = 0;
    int errors = 0;

    for (size_t i = 0; i < formulas.size(); ++i) {
        const auto& formula = formulas[i];
        BenchResult br;
        br.formula = formula;

        std::cout << "[" << (i + 1) << "/" << formulas.size() << "] " << formula << std::flush;

        // --- Time ltlsynt (using its self-reported internal time) ---
        SynthesisResult ltlsynt_res = solve_with_ltlsynt(formula, synt_opts);
        if (ltlsynt_res.error) {
            br.error = true;
            br.error_msg = ltlsynt_res.error_msg;
            br.match = false;
            errors++;
            std::cerr << "\n  LTLSYNT ERROR: " << ltlsynt_res.error_msg << "\n";
            results.push_back(br);
            continue;
        }
        br.ltlsynt_ms = ltlsynt_res.internal_time_ms;
        br.ltlsynt_realizable = ltlsynt_res.realizable;

        // --- Build automaton ---
        spot::twa_graph_ptr automaton = generate_automata(formula, synt_opts.inputs, synt_opts.outputs);
        br.num_states = automaton->num_states();
        {
            std::ostringstream acc_ss;
            acc_ss << automaton->get_acceptance();
            br.acceptance = acc_ss.str();
        }

        // --- Time SolveAutEL_swa ---
        try {
            auto t2 = std::chrono::high_resolution_clock::now();
            StateSet w_partition_swa = SolveAutEL_swa(automaton, synt_opts.outputs);
            auto t3 = std::chrono::high_resolution_clock::now();
            br.swa_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

            int starting_state = 0;
            br.swa_realizable = w_partition_swa.count(starting_state) > 0;

            if (br.swa_realizable != br.ltlsynt_realizable) {
                br.match = false;
                mismatches++;
                std::cerr << "\n  MISMATCH: ltlsynt=" << (br.ltlsynt_realizable ? "R" : "U")
                          << " swa=" << (br.swa_realizable ? "R" : "U") << "\n";
            }
        } catch (const std::exception& e) {
            br.error = true;
            br.error_msg = e.what();
            br.swa_ms = -1;
            br.match = false;
            errors++;
            std::cerr << "\n  ERROR: " << e.what() << "\n";
        }

        double speedup = (br.swa_ms > 0 && br.ltlsynt_ms > 0) ? br.ltlsynt_ms / br.swa_ms : 0;
        std::string status_tag;
        if (!br.match) status_tag = "  *** MISMATCH ***";

        std::cout << "  states=" << br.num_states
                  << "  ltlsynt=" << std::fixed << std::setprecision(1) << br.ltlsynt_ms << "ms"
                  << "  swa=" << br.swa_ms << "ms"
                  << "  speedup=" << std::setprecision(2) << speedup << "x"
                  << status_tag
                  << "\n";

        results.push_back(br);
    }

    // --- Write CSV ---
    std::ofstream csv(csv_path);
    csv << "formula,num_states,acceptance,ltlsynt_realizable,swa_realizable,"
        << "ltlsynt_ms,swa_ms,speedup,match,error,error_msg\n";
    for (const auto& r : results) {
        // Escape formula for CSV (wrap in quotes, escape inner quotes)
        std::string escaped = r.formula;
        for (size_t pos = 0; (pos = escaped.find('"', pos)) != std::string::npos; pos += 2)
            escaped.insert(pos, 1, '"');

        double speedup = (r.swa_ms > 0 && r.ltlsynt_ms > 0) ? r.ltlsynt_ms / r.swa_ms : 0;
        csv << "\"" << escaped << "\","
            << r.num_states << ","
            << "\"" << r.acceptance << "\","
            << (r.ltlsynt_realizable ? "true" : "false") << ","
            << (r.swa_realizable ? "true" : "false") << ","
            << std::fixed << std::setprecision(3)
            << r.ltlsynt_ms << ","
            << r.swa_ms << ","
            << std::setprecision(4) << speedup << ","
            << (r.match ? "true" : "false") << ","
            << (r.error ? "true" : "false") << ","
            << "\"" << r.error_msg << "\"\n";
    }
    csv.close();

    // --- Summary statistics ---
    std::cout << "\n========== BENCHMARK SUMMARY ==========\n";
    std::cout << "Total formulas:    " << results.size() << "\n";
    std::cout << "Mismatches:        " << mismatches << "\n";
    std::cout << "Errors:            " << errors << "\n";

    // Compute timing statistics (only for non-error results)
    std::vector<double> ltlsynt_times, swa_times, speedups;
    int realizable_count = 0;
    for (const auto& r : results) {
        if (!r.error) {
            ltlsynt_times.push_back(r.ltlsynt_ms);
            swa_times.push_back(r.swa_ms);
            if (r.swa_ms > 0)
                speedups.push_back(r.ltlsynt_ms / r.swa_ms);
        }
        if (r.ltlsynt_realizable)
            realizable_count++;
    }

    if (!ltlsynt_times.empty()) {
        auto sum = [](const std::vector<double>& v) {
            return std::accumulate(v.begin(), v.end(), 0.0);
        };
        auto median = [](std::vector<double> v) -> double {
            std::sort(v.begin(), v.end());
            size_t n = v.size();
            return (n % 2 == 0) ? (v[n/2 - 1] + v[n/2]) / 2.0 : v[n/2];
        };

        size_t n = ltlsynt_times.size();
        double ltlsynt_total = sum(ltlsynt_times);
        double swa_total = sum(swa_times);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Realizable:        " << realizable_count << "/" << results.size() << "\n";
        std::cout << "\n--- Timing (ms) ---\n";
        std::cout << "                   ltlsynt        swa\n";
        std::cout << "  Total:      " << std::setw(10) << ltlsynt_total
                  << "  " << std::setw(10) << swa_total << "\n";
        std::cout << "  Mean:       " << std::setw(10) << ltlsynt_total / n
                  << "  " << std::setw(10) << swa_total / n << "\n";
        std::cout << "  Median:     " << std::setw(10) << median(ltlsynt_times)
                  << "  " << std::setw(10) << median(swa_times) << "\n";
        std::cout << "  Min:        " << std::setw(10) << *std::min_element(ltlsynt_times.begin(), ltlsynt_times.end())
                  << "  " << std::setw(10) << *std::min_element(swa_times.begin(), swa_times.end()) << "\n";
        std::cout << "  Max:        " << std::setw(10) << *std::max_element(ltlsynt_times.begin(), ltlsynt_times.end())
                  << "  " << std::setw(10) << *std::max_element(swa_times.begin(), swa_times.end()) << "\n";

        if (!speedups.empty()) {
            std::cout << "\n--- Speedup (ltlsynt_time / swa_time) ---\n";
            std::cout << "  Mean:   " << sum(speedups) / speedups.size() << "x\n";
            std::cout << "  Median: " << median(speedups) << "x\n";
            std::cout << "  Min:    " << *std::min_element(speedups.begin(), speedups.end()) << "x\n";
            std::cout << "  Max:    " << *std::max_element(speedups.begin(), speedups.end()) << "x\n";
        }

        std::cout << "\nOverall speedup (total time): "
                  << std::setprecision(2) << ltlsynt_total / swa_total << "x\n";
    }

    std::cout << "\nCSV results written to: " << csv_path << "\n";
    std::cout << "========================================\n";

    return mismatches > 0 ? 1 : 0;
}

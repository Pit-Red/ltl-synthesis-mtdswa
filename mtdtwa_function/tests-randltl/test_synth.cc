#include "ltlgen.hh"
#include "ltlsynt.hh"
#include "../solve_el_aut.hh"
#include "../solve_el_aut_swa.hh"

#include <iostream>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/zlktree.hh>
#include <spot/twaalgos/mtdtwa.hh>
#include <spot/twaalgos/game.hh>
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

    // Building the autmaton from the extracted information
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

int main(int argc, char const *argv[])
{
    if (argc < 5)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <ltl_input> <ltlsynt_output> <random_seed> <number_of_samples> [tree_size]\n";
        return 1;
    }

    std::string ltl_input = argv[1];
    std::string ltlsynt_output = argv[2];
    int random_seed = std::stoi(argv[3]);
    int number_of_samples = std::stoi(argv[4]);
    std::string tree_size = (argc > 5) ? argv[5] : "100";

    std::vector<std::string> atomic_props_input;
    std::istringstream iss(ltl_input);
    std::string ap;
    while (std::getline(iss, ap, ',')) {
        atomic_props_input.push_back(ap);
    }

    std::vector<std::string> atomic_props_output;
    std::istringstream iss_out(ltlsynt_output);
    while (std::getline(iss_out, ap, ',')) {
        atomic_props_output.push_back(ap);
    }

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
    synt_opts.algo = "acd";
    synt_opts.simplify = "no";
    synt_opts.decompose = "yes";
    synt_opts.bypass = "yes";

    int count = 0;
    int mismatches = 0;
    for (size_t i = 0; i < formulas.size(); ++i) {
        const auto& formula = formulas[i];
        std::cout << "[" << (i + 1) << "/" << formulas.size() << "] Formula: " << formula << "\n";

        SynthesisResult res = solve_with_ltlsynt(formula, synt_opts);
        std::cout << "Realizable (ltlsynt): " << (res.realizable ? "Yes" : "No") << "\n";
        if (res.realizable)
            count++;

        spot::twa_graph_ptr automaton = generate_automata(formula, synt_opts.inputs, synt_opts.outputs);
        std::cout << "Automaton acceptance condition: " << automaton->get_acceptance() << "\n";

        // EL_SOLVER env: "twa" = mtdtwa only, "swa" = mtdswa only, "both" or unset = both
        const char* solver_env = std::getenv("EL_SOLVER");
        std::string solver_mode = solver_env ? solver_env : "both";
        bool run_twa = (solver_mode == "twa" || solver_mode == "both");
        bool run_swa = (solver_mode == "swa" || solver_mode == "both");

        try
        {
            std::cout << "Number of states in the automaton: " << automaton->num_states() << "\n";
            int starting_state = 0; // Assuming the initial state is always 0 for the generated automata

            if (run_twa) {
                StateSet w_partition = SolveAutEL(automaton, synt_opts.outputs);
                bool el_realizable = w_partition.count(starting_state) > 0;
                std::cout << "Realizable (mtdtwa): " << (el_realizable ? "Yes" : "No") << "\n";

                if (el_realizable != res.realizable) {
                    mismatches++;
                    std::cerr << "MISMATCH: ltlsynt=" << (res.realizable ? "REALIZABLE" : "UNREALIZABLE")
                              << " mtdtwa=" << (el_realizable ? "REALIZABLE" : "UNREALIZABLE")
                              << " formula=" << formula << "\n";
                }
            }

            if (run_swa) {
                StateSet w_partition_swa = SolveAutEL_swa(automaton, synt_opts.outputs);
                bool swa_realizable = w_partition_swa.count(starting_state) > 0;
                std::cout << "Realizable (mtdswa): " << (swa_realizable ? "Yes" : "No") << "\n";

                if (swa_realizable != res.realizable) {
                    mismatches++;
                    std::cerr << "MISMATCH: ltlsynt=" << (res.realizable ? "REALIZABLE" : "UNREALIZABLE")
                              << " mtdswa=" << (swa_realizable ? "REALIZABLE" : "UNREALIZABLE")
                              << " formula=" << formula << "\n";
                }
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << "ERROR: " << e.what() << '\n';
        }

        std::cout << "-------------------------\n";
        std::cout.flush();
    }

    std::cout << "Total formulas: " << formulas.size() << "\n";
    std::cout << "Realizable formulas: " << count << "\n";
    std::cout << "Mismatches: " << mismatches << "\n";

    return mismatches > 0 ? 1 : 0;
}
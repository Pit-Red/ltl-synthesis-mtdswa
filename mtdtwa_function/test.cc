#include <spot/tl/parse.hh>
#include "solve_el_aut.hh"
#include <spot/twaalgos/translate.hh>
#include <spot/twaalgos/zlktree.hh>
#include <spot/twaalgos/mtdtwa.hh>
#include <spot/twaalgos/dot.hh>
#include <spot/twaalgos/game.hh>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>   



void save_datastructures(spot::twa_graph_ptr aut) {

    spot::mtdtwa_ptr mtdta = spot::dtwa_to_mtdtwa(aut);
    spot::zielonka_tree zl_tree = spot::zielonka_tree(aut->acc());

    std::string dot_folder = "img/";

    std::vector<std::string> names_datastructures = {"aut", "mtdta", "zl_tree"};

    for (const auto& name : names_datastructures) {
        std::string dot_path = dot_folder + name + ".dot";
        std::ofstream dot_out(dot_path);
        if (!dot_out) {
            std::cerr << "Error: cannot write to " << dot_path << "\n";
            return;
        }

        if (name == "aut")
            spot::print_dot(dot_out, aut);
        else if (name == "mtdta")
            mtdta->print_dot(dot_out);
        else if (name == "zl_tree")
            zl_tree.dot(dot_out);

        dot_out.close();
         
        // printing the PNG
        std::string png_path = dot_folder + name + ".png";
        std::string cmd = "dot -Tpng " + dot_path + " -o " + png_path;
        int ret = std::system(cmd.c_str());
        if (ret != 0)
            std::cerr << "Warning: GraphViz 'dot' failed (code " << ret << ")\n";
        else
            std::cout << "PNG saved to " << png_path << "\n";

        std::remove(dot_path.c_str());
    }

}

std::vector<std::string> read_formulas_from_stdin() {
    std::vector<std::string> formulas;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;   // skip empty lines
        formulas.push_back(line);
    }
    return formulas;
}

int main(int argc, char** argv) {

    // Usage: el_solver <formula> [controllable_vars]
    //   controllable_vars is a comma-separated list, e.g. "g1,g2"
    //   If omitted, no variables are controllable.
    //   Formula can also be piped via stdin (one formula per line).

    std::vector<std::string> formulas_str;

    if (argc >= 2) {
        // Formula provided as argument
        formulas_str.push_back(argv[1]);
    } else {
        // Maybe piped input?
        if (!std::cin.good()) {
            std::cerr << "Usage: " << argv[0]
                      << " <formula> [controllable_vars]\n"
                      << "  controllable_vars: comma-separated, e.g. g1,g2\n";
            return 1;
        }

        formulas_str = read_formulas_from_stdin();

        if (formulas_str.empty()) {
            std::cerr << "Error: no formula read from stdin.\n";
            return 1;
        }
    }

    // Parse controllable variables from argv[2] (comma-separated)
    std::vector<std::string> controllable;
    if (argc >= 3) {
        std::istringstream iss(argv[2]);
        std::string var;
        while (std::getline(iss, var, ',')) {
            if (!var.empty())
                controllable.push_back(var);
        }
    }

    // Do the conjunction of all the formulas in the input
    std::string formula_str = "true";
    for (const auto& f_str : formulas_str) {
        formula_str = formula_str + " & (" + f_str + ")";
    }

    std::cout << "Combined formula: " << formula_str << "\n";

    // Parse formula
    spot::formula f = spot::parse_formula(formula_str);

    std::cout << "Parsed formula: " << f << "\n";

    if (!f) {
        std::cerr << "Error: invalid formula: " << formula_str << "\n";
        return 1;
    }

    // Register input APs first so the MTDTWA tape order matches
    // Mealy semantics (inputs before outputs).
    spot::bdd_dict_preorder dict;
    std::unordered_set<spot::formula> input_aps;

    // APs that are NOT controllable are inputs
    f.traverse([&](const spot::formula& sub) {
        if (sub.is(spot::op::ap)) {
            std::string name = sub.ap_name();
            bool is_controllable = false;
            for (const auto& c : controllable)
                if (c == name) { is_controllable = true; break; }
            if (!is_controllable)
                input_aps.insert(sub);
        }
        return false;
    });
    for (const auto& ap : input_aps)
        dict.register_proposition(ap);

    // Deterministic, complete, generic acceptance
    spot::translator trans(dict);
    trans.set_type(spot::postprocessor::Generic);
    trans.set_pref(spot::postprocessor::Deterministic |
                   spot::postprocessor::Complete);

    auto aut = trans.run(f);

    std::cout << "Automaton has " << aut->num_states()
              << " states, acceptance = " << aut->acc()
              << ", initial state: " << aut->get_init_state_number()
              << "\n";

    std::cout << "Controllable: ";
    if (controllable.empty()) std::cout << "(none)";
    for (const auto& c : controllable) std::cout << c << " ";
    std::cout << "\n";

    for (auto var: aut->ap())
        std::cout << "AP: " << var << "\n";

    save_datastructures(aut);

    // EL solver
    StateSet win_el = SolveAutEL(aut, controllable);

    std::cout << "EL winning states: ";
    for (auto s : win_el)
        std::cout << s << " ";
    std::cout << "\n";

    bool realizable = win_el.count(aut->get_init_state_number()) > 0;
    std::cout << "Realizable: " << (realizable ? "Yes" : "No") << "\n";

    return 0;
}

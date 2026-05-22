#include <spot/twaalgos/mtdtwa.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/dot.hh>
#include <spot/tl/formula.hh>
#include <spot/twa/bdddict.hh>
#include <spot/twaalgos/determinize.hh>
#include <spot/twaalgos/isdet.hh>
#include <iostream>
#include <fstream>
#include <../buddy/src/cache.h>
#include <sstream>

// Global variables
static std::vector<bool>* global_is_winning = nullptr;
static const spot::acc_cond::mark_t* global_acc_set = nullptr;
static const spot::mtdtwa_ptr* global_mtdta = nullptr;


// static adjlist<void>
//   build_reverse_of_reachable_graph(mtdfa_ptr dfa)
//   {
//     unsigned n = dfa->num_roots();
//     adjlist<void> reverse(n, n);
//     reverse.new_states(n);

//     std::queue<int> todo;
//     std::vector<bool> seen(n, false); // added to todo
//     std::vector<int> seen_local(n, -1); // seen as predecessor of src
//     todo.push(0);
//     seen[0] = true;
//     while (!todo.empty()) // just iterate over the states
//       {
//         int src = todo.front();
//         todo.pop();
//         bool has_acc = false;   // already seen an accepting terminal
//         for (auto t: silent_paths_mt_of(dfa->states[src]))
//           {
//             if (t == bddfalse)
//               continue;
//             if (t == bddtrue)
//               {
//                 if (!has_acc)
//                   {
//                     reverse.new_edge(0, src);
//                     has_acc = true;
//                   }
//                 continue;
//               }
//             int dst = bdd_get_terminal(t);
//             if (dst & 1) // Handle the acceptance colors and for each color add the src state
//                          // to the list of states that can reach this color
//               {
//                 if (!has_acc)
//                   {
//                     reverse.new_edge(0, src);
//                     has_acc = true;
//                   }
//                 continue;
//               }
//             dst /= 2; // handle state and the adjacency list by saying src is a predecessor of dst
//             // we don't record predecessors from 0, as they are not
//             // needed for backward propagation.
//             if (dst == 0)
//               continue;
//             if (seen_local[dst] == src)
//               continue;
//             seen_local[dst] = src;
//             reverse.new_edge(dst, src);
//             if (!seen[dst])
//               {
//                 todo.push(dst);
//                 seen[dst] = true;
//               }
//           }
//       }
//     return reverse;
//   }


// Function to estimate size for BDD cache
static size_t size_estimate_unary(const spot::mtdtwa_ptr& mtdta) {
    return mtdta->num_roots() * 2;
}

// Function to check if a terminal is winning
static int is_winning_terminal_with_acc(int v) {
    int dst = v;
    std::cout << "Checking terminal " << dst << std::endl;
    const auto& d = (*global_mtdta)->terminal_data[dst];
    return (((d.first & *global_acc_set) != spot::acc_cond::mark_t{}) || (*global_is_winning)[dst]);
}

void print_terminal_data(const spot::mtdtwa_ptr& mtdta) {
    // Print terminal data in the same format as Python
    for (size_t i = 0; i < mtdta->terminal_data.size(); ++i) {
        const auto& d = mtdta->terminal_data[i];
        std::cout << "terminal_data[" << i << "] = (";
        
        // Print acceptance set
        std::cout << "{";
        bool first = true;
        for (int part : d.first.sets()) {
            if (!first) std::cout << ", ";
            std::cout << part;
            first = false;
        }
        std::cout << "}, ";
        
        // Print destination state
        std::cout << d.second << ")" << std::endl;
    }
}

// Helper function to get state information
static std::string get_state_info(const spot::mtdtwa_ptr& mtdta, int state_id) {
    if (state_id < 0) return "Invalid State";
    
    // Get the state's BDD
    bdd state_bdd = mtdta->states[state_id];
    
    // Get the terminal data for this state
    const auto& term_data = mtdta->terminal_data[state_id];
    
    std::stringstream ss;
    ss << "State " << state_id << " (";
    
    // Print acceptance set
    ss << "Acc: {";
    bool first = true;
    for (int part : term_data.first.sets()) {
        if (!first) ss << ", ";
        ss << part;
        first = false;
    }
    ss << "}, ";
    
    // Print destination
    ss << "Dest: " << term_data.second << ")";
    
    return ss.str();
}

std::vector<bool> attractor_states(const spot::mtdtwa_ptr& mtdta, const spot::acc_cond::mark_t& acc_set, const bdd& controllable_vars) {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_unary(mtdta), false);
    int iteration = 0;

    unsigned nroots = mtdta->num_roots();
    std::vector<bool> winning(nroots, false);
    global_is_winning = &winning;
    global_acc_set = &acc_set;
    global_mtdta = &mtdta;

    // Prepare for quantification over controllable variables
    bdd_mt_quantify_prepare(controllable_vars);

    bool has_changed;
    do {

        std::cout << "\n\n\033[31m Iteration " << iteration << ": \033[0m\n\n";

        has_changed = false;
        for (unsigned i = 0; i < nroots; ++i) {
            if (winning[i])
                continue;
            bdd b = mtdta->states[i];
            
            // For controllable variables, we can choose the winning path
            // For uncontrollable variables, we need all paths to be winning
            if (bdd_mt_quantify_to_bool(b, is_winning_terminal_with_acc, &cache, iteration)) {
                has_changed = true;
                winning[i] = true;
            }
        }

        // Print the winning vector
        std::cout << "\nCurrent winning vector: ";
        for (size_t i = 0; i < winning.size(); ++i) {
            std::cout << (winning[i] ? "1" : "0");
        }
        std::cout << "\n\n";

        
        ++iteration;
    } while (has_changed);

    bdd_extcache_done(&cache);
    return winning;
}

// Helper function to get variable index from name
int get_var_index(const spot::twa_graph_ptr& aut, const std::string& var_name) {
    spot::formula f = spot::formula::ap(var_name);
    return aut->get_dict()->register_proposition(f, aut.get());
}

// Function to convert vector of variable names to BDD
bdd variables_to_bdd(const spot::twa_graph_ptr& aut, const std::vector<std::string>& var_names) {
    bdd result = bddtrue;
    for (const auto& var_name : var_names) {
        int var_index = get_var_index(aut, var_name);
        result &= bdd_ithvar(var_index);
    }
    return result;
}

int main()
{

    // https://www.lrde.epita.fr/~adl/_synt/mttwa.html
    // Create the LTL formula
    spot::formula f = spot::parse_formula("F a");
    
    // Create a translator with generic acceptance and deterministic output
    spot::translator trans;
    trans.set_type(spot::postprocessor::Generic);  // equivalent to "generic"
    trans.set_pref(spot::postprocessor::Deterministic | spot::postprocessor::Complete);
    
    // Translate the formula to an automaton
    spot::twa_graph_ptr aut = trans.run(f);
    
    // Print variable information
    std::cout << "Formula variables and their indices:" << std::endl;
    for (auto ap: aut->ap()) {
        std::cout << "Variable '" << ap.ap_name() << "' has index " 
                  << get_var_index(aut, ap.ap_name()) << std::endl;
    }
    
    // Convert to MTDTA 
    auto mtdta = spot::dtwa_to_mtdtwa(aut);
    
    // Create an acceptance set to check against
    spot::acc_cond::mark_t acc_set{0}; 
    std::cout << "\nChecking for acceptance set: " << acc_set << std::endl;
    
    // Define controllable variables using their names
    std::vector<std::string> controllable_var_names = {"a"};
    bdd controllable_vars = variables_to_bdd(aut, controllable_var_names);
    
    
    // Print the BDD structure using mtdtwa's print_dot
    std::cout << "\nBDD structure for controllable variables:" << std::endl;
    std::ofstream dot_file("bdd.dot");
    if (dot_file) {
        mtdta->print_dot(dot_file);
        dot_file.close();
        std::cout << "BDD visualization saved to controllable_vars.dot" << std::endl;
    }
    
    // Compute and print attractor states with controllable variables
    std::vector<bool> winning = attractor_states(mtdta, acc_set, controllable_vars);
    
    std::cout << "\nAttractor States:" << std::endl;
    for (size_t i = 0; i < winning.size(); ++i) {
        std::cout << "State " << i << ": " << (winning[i] ? "Winning" : "Losing") << std::endl;
    }

    

    return 0;
} 
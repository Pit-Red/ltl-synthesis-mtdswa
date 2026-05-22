


#include "HierachicalChecker.hh"
#include "solve_el_aut.hh"

#include <spot/twaalgos/zlktree.hh>
#include <spot/twaalgos/mtdtwa.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/dot.hh>
#include <spot/tl/formula.hh>
#include <spot/twa/bdddict.hh>
#include <spot/twaalgos/determinize.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/graph/adjlist.hh>
#include <iostream>
#include <fstream>
//#include "../buddy/src/cache.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <utility>
#include <cstdlib>

// =========================================================================
// Tracing infrastructure — enable with EL_TRACE=1 environment variable
// =========================================================================
static bool trace_enabled()
{
    static int val = -1;
    if (val < 0) {
        const char* env = std::getenv("EL_TRACE");
        val = (env && std::string(env) != "0") ? 1 : 0;
    }
    return val;
}

static int trace_depth = 0; // recursion depth for indentation

// ANSI color codes
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_BLUE    "\033[34m"
#define CLR_MAGENTA "\033[35m"
#define CLR_CYAN    "\033[36m"
#define CLR_DIM     "\033[2m"

static std::string trace_indent()
{
    std::string s;
    for (int i = 0; i < trace_depth; ++i)
        s += "  \u2502 ";  // "│ "
    return s;
}

// Format a StateSet as {0, 2, 5}
static std::string fmt_set(const StateSet& ss)
{
    std::ostringstream o;
    o << "{";
    bool first = true;
    // Sort for readable output
    std::vector<unsigned> sorted(ss.begin(), ss.end());
    std::sort(sorted.begin(), sorted.end());
    for (auto s : sorted) {
        if (!first) o << ", ";
        o << s;
        first = false;
    }
    o << "}";
    return o.str();
}

static std::string fmt_marks(const OrderedMarkList& marks)
{
    std::ostringstream o;
    o << "[";
    bool first = true;
    for (const auto& mp : marks) {
        if (!first) o << ", ";
        o << mp.first << ":" << (mp.second == 0 ? "P0" : "P1");
        first = false;
    }
    o << "]";
    return o.str();
}

static std::string fmt_player(unsigned p)
{
    return p == 0
        ? std::string(CLR_GREEN) + "P0/sys" + CLR_RESET
        : std::string(CLR_RED)   + "P1/env" + CLR_RESET;
}

#define TRACE(...)                                  \
    do { if (trace_enabled()) {                     \
        std::cerr << trace_indent() << __VA_ARGS__; \
    } } while(0)

// --- helper aliases ---
using StateSet  = std::unordered_set<spot::twa_graph::state_num>;
using StateSetList = std::vector<StateSet>;
using Mark      = spot::acc_cond::mark_t;
using ZlNodePtr = spot::zielonka_tree::zielonka_node*;
using ZlPtr = spot::zielonka_tree*;



static std::vector<bool>* global_is_winning = nullptr;
static const HierachicalChecker* global_hierarchical_checker = nullptr;
static const spot::mtdtwa_ptr* global_mtdta = nullptr;
static const StateSet* global_states = nullptr; // V
static const std::vector<spot::formula>* global_atomic_prop = nullptr;
static const std::vector<std::string>* global_controllable_var_names = nullptr;
static const std::vector<std::string>* global_inverted_controllable_var_names = nullptr;
static const spot::twa_graph_ptr* global_aut = nullptr;
static int global_current_player = 0;
static unsigned global_accepting_sink_terminal = -1U; // terminal index of the accepting sink
static unsigned global_rejecting_sink_terminal = -1U; // terminal index of the rejecting sink
static int global_accepting_player = 0; // player who wins from accepting plays


// alias for readability: map from mark/state → **set** of ints
using SetMap = std::unordered_map<int, std::unordered_set<int>>;

// Function to check if a terminal is winning
static int is_winning_terminal_with_acc(int v) {
    unsigned dst = v;

    // Accepting sink: always winning for the accepting player.
    if (dst == global_accepting_sink_terminal)
        return global_current_player == global_accepting_player;

    // Rejecting sink: always winning for the non-accepting player.
    if (dst == global_rejecting_sink_terminal)
        return global_current_player != global_accepting_player;

    const auto& d = (*global_mtdta)->terminal_data_map[dst];
    bool acc_win = global_hierarchical_checker->check(d.first, global_current_player);
    if (acc_win)
        TRACE(CLR_DIM "    winning terminal: state=" << d.second << " acc_mark=" << d.first << CLR_RESET "\n");
    return acc_win || (*global_is_winning)[d.second];
}

// Function to estimate size for BDD cache
static size_t size_estimate_unary(const spot::mtdtwa_ptr& mtdta) {
    return mtdta->num_roots() * 2;
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


StateSet attractor_states(
    const spot::mtdtwa_ptr& mtdta,
    const HierachicalChecker& hc,
    const std::vector<std::string>& controllable_var_names,
    const StateSet& seed,
    const int current_player
) {
    TRACE(CLR_DIM "attractor(" << fmt_player(current_player)
          << CLR_DIM << ", w_acc=" << fmt_marks(hc.get_ordered_marks())
          << ", seed=" << fmt_set(seed) << ")" CLR_RESET "\n");

    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_unary(mtdta), false);
    int iteration = 0;

    unsigned nroots = mtdta->num_roots();
    std::vector<bool> winning(nroots, false);
    global_is_winning = &winning;
    global_hierarchical_checker = &hc;
    global_mtdta = &mtdta;
    global_current_player = current_player;

    // Prepare for quantification over controllable variables
    bdd quantified_vars;

    if (current_player == 1)
        quantified_vars = variables_to_bdd(*global_aut, *global_inverted_controllable_var_names);
    else
        quantified_vars = variables_to_bdd(*global_aut, *global_controllable_var_names);

    bdd_mt_quantify_prepare(quantified_vars);

    for (unsigned i = 0; i < nroots; ++i) {
        if (seed.count(i) > 0)
            winning[i] = true;
    }



    bool has_changed;
    do {


        has_changed = false;
        for (unsigned i = 0; i < nroots; ++i) {

            // std::cout << "  checking state " << i <<"\n";

            if (winning[i])
                continue;
            bdd b = mtdta->states[i];

            // For controllable variables, we can choose the winning path
            // For uncontrollable variables, we need all paths to be winning
            if (bdd_mt_quantify_to_bool(b, is_winning_terminal_with_acc, &cache, iteration)) {
                has_changed = true;
                winning[i] = true;
                TRACE(CLR_DIM "  attractor iter " << iteration
                      << ": state " << i << " now winning" CLR_RESET "\n");
            }
        }


        ++iteration;
    } while (has_changed);

    bdd_extcache_done(&cache);

    StateSet winning_states;
    for (unsigned i = 0; i < nroots; ++i) {
        if (winning[i]) {
            winning_states.insert(i);
        }
    }

    TRACE(CLR_DIM "  attractor result = " << fmt_set(winning_states)
          << " (" << iteration << " iters)" CLR_RESET "\n");
    return winning_states;
}


bool equal_statesets(const StateSet& a, const StateSet& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (const auto& elem : a) {
        if (b.find(elem) == b.end()) {
            return false;
        }
    }
    return true;
}

StateSet union_statesets(const StateSet& a, const StateSet& b) {
    StateSet result = a;
    for (const auto& elem : b) {
        result.insert(elem);
    }
    return result;
}

StateSet difference_statesets(const StateSet& a, const StateSet& b) {
    StateSet result;
    for (const auto& elem : a) {
        if (b.find(elem) == b.end()) {
            result.insert(elem);
        }
    }
    return result;
}

StateSet
SolveAutEL_t(
    StateSet& w_partition,
    StateSet& l_partition,
    ZlPtr zl_tree,
    unsigned int zl_node_idx, // index of the parent node
    HierachicalChecker* hc,
    const bool is_even
)
{

    ZlNodePtr zl_node_parent = &zl_tree->nodes_[zl_node_idx]; // t_0

    unsigned int w_player = is_even ? 0 : 1; 
    unsigned int l_player = is_even ? 1 : 0;

    TRACE(CLR_BOLD CLR_CYAN "\u250c\u2500 SolveAutEL_t" CLR_RESET
          " zlk_node=" << zl_node_idx
          << " level=" << zl_node_parent->level
          << " colors=" << zl_node_parent->colors
          << " w_player=" << fmt_player(w_player)
          << " l_player=" << fmt_player(l_player) << "\n");
    TRACE("  W=" << fmt_set(w_partition)
          << "  L=" << fmt_set(l_partition)
          << "  V=" << fmt_set(*global_states) << "\n");
    ++trace_depth;

    // The original algorithm tail-recurses when l_partition grows:
    //   return SolveAutEL_t(w_partition, l_partition_new, ..., zl_node_idx, ...)
    // Only l_partition changes between calls; everything else is the same.
    // We eliminate the tail recursion with a loop to avoid stack overflow on
    // large automata (l_partition can grow O(n) times before stabilising).
restart:
    {
        // Iterating over siblings:
        unsigned int starting_index = zl_node_parent->first_child;

        // Base case: leaf node — all non-losing states are winning for w_player
        if (starting_index == 0) {
            StateSet base_result = difference_statesets(*global_states, l_partition);
            TRACE(CLR_YELLOW "\u2514 LEAF" CLR_RESET " -> return V\\L = "
                  << fmt_set(base_result) << "\n");
            --trace_depth;
            return base_result;
        }

        // result holds the winning set computed by the last sibling processed
        StateSet result = difference_statesets(*global_states, l_partition);

        unsigned int it_index = starting_index;
        int sibling_num = 0;
        do
        {
            ZlNodePtr zl_node = &zl_tree->nodes_[it_index]; // t_1

            TRACE(CLR_BOLD CLR_BLUE "\u251c\u2500 sibling #" << sibling_num
                  << CLR_RESET " zlk_node=" << it_index
                  << " level=" << zl_node->level
                  << " colors=" << zl_node->colors << "\n");

            StateSet L;

            // Reset fixed-point state for each sibling
            StateSetList W{1};
            W[0] = difference_statesets(*global_states, l_partition);
            int j = 0;

            // Setting up f_prime
            HierachicalChecker* hc_prime = hc->new_instance(zl_node_parent->colors.operator-(zl_node->colors), w_player);
            struct HcGuard { HierachicalChecker* p; ~HcGuard() { delete p; } } hc_prime_guard{hc_prime};

            TRACE(CLR_MAGENTA "  initial attractor for " << fmt_player(w_player)
                  << CLR_RESET "\n");
            W[0] = attractor_states(
                    *global_mtdta,
                    *hc_prime,
                    *global_controllable_var_names,
                    w_partition,
                    w_player
                );
            TRACE("  W[0] = " << fmt_set(W[0]) << "\n");

            for (; j == 0 || !equal_statesets(W[2*j], W[2*j-2]); j++){
                W.resize(2*j + 3); // ensure space for W[2j + 1] and W[2j + 2]

                TRACE(CLR_BOLD "\u251c\u2500 fixed-point iter j=" << j << CLR_RESET
                      << "  W[" << 2*j << "]=" << fmt_set(W[2*j]) << "\n");

                W[2*j + 1] = W[2*j]; // initialize to W[2j]

                // Iterating over children of the current Zielonka node
                unsigned int starting_child_index = zl_node->first_child;

                unsigned int child_it_index = starting_child_index;
                int child_num = 0;
                do
                {
                    if (starting_child_index == 0) {
                        TRACE("  (no children)\n");
                        break;
                    }

                    ZlNodePtr zl_child_node = &zl_tree->nodes_[child_it_index]; // t_2

                    TRACE(CLR_CYAN "  \u251c child #" << child_num
                          << CLR_RESET " zlk_node=" << child_it_index
                          << " colors=" << zl_child_node->colors << "\n");

                    // Setting up f_double_prime
                    HierachicalChecker* hc_double_prime = hc_prime->new_instance(zl_node->colors.operator-(zl_child_node->colors));
                    struct HcGuard2 { HierachicalChecker* p; ~HcGuard2() { delete p; } } hc_double_prime_guard{hc_double_prime};

                    TRACE("    L-attractor for " << fmt_player(l_player) << "\n");
                    L = attractor_states(
                        *global_mtdta,
                        *hc_double_prime,
                        *global_controllable_var_names,
                        l_partition,
                        l_player  // switch player
                    );
                    TRACE("    L = " << fmt_set(L) << "\n");

                    // Restrict the recursive subgame to W[2*j]: treat
                    // states outside W[2*j] as already lost so the base
                    // case can only return states within W[2*j],
                    // preserving the descending monotonicity the j-loop
                    // needs to converge.
                    StateSet restricted_l = union_statesets(
                        L,
                        difference_statesets(*global_states, W[2*j])
                    );

                    TRACE("    " CLR_BOLD "RECURSE" CLR_RESET " into zlk_node="
                          << child_it_index << " with restricted_l="
                          << fmt_set(restricted_l) << "\n");
                    W[2*j + 1] = SolveAutEL_t(
                        W[2*j],
                        restricted_l,
                        zl_tree,
                        child_it_index,
                        hc_double_prime,
                        is_even
                    );
                    TRACE("    returned W[" << 2*j+1 << "] = "
                          << fmt_set(W[2*j + 1]) << "\n");

                    if (!equal_statesets(W[2*j + 1], W[2*j])) {
                        TRACE("    " CLR_RED "W shrunk!" CLR_RESET
                              " W[" << 2*j+1 << "] != W[" << 2*j
                              << "] -> break children loop\n");
                        break;
                    }

                    child_it_index = zl_child_node->next_sibling;
                    ++child_num;

                }while (child_it_index != starting_child_index);

                TRACE("  W-attractor for " << fmt_player(w_player) << "\n");
                W[2*j + 2] = attractor_states(
                    *global_mtdta,
                    *hc_prime,
                    *global_controllable_var_names,
                    W[2*j + 1],
                    w_player
                );
                TRACE("  W[" << 2*j+2 << "] = " << fmt_set(W[2*j + 2]) << "\n");
            }

            TRACE("  fixed-point converged at j=" << j
                  << "  W[" << 2*j << "]=" << fmt_set(W[2*j]) << "\n");

            if (!equal_statesets(union_statesets(W[2*j], l_partition), *global_states)) {
                // l_partition has not yet absorbed all states — extend it and retry.
                l_partition = union_statesets(l_partition, difference_statesets(*global_states, W[2*j]));
                TRACE(CLR_RED CLR_BOLD "  RESTART" CLR_RESET
                      " W[2j] \u222a L != V, growing l_partition\n");
                TRACE("  l_partition = " << fmt_set(l_partition)
                      << " -> goto restart\n");
                goto restart; // l_partition strictly grew; bounded by |states|
            }

            result = W[2*j];
            it_index = zl_node->next_sibling;
            ++sibling_num;

        }
        while (it_index != starting_index);

        TRACE(CLR_GREEN CLR_BOLD "\u2514 RETURN" CLR_RESET " "
              << fmt_set(result) << "\n");
        --trace_depth;
        return result;
    }

}

std::vector<std::string> get_inverted_variable_names(
    const std::vector<std::string>& controllable_var_names,
    const std::vector<spot::formula>& atomic_prop
) {
    std::vector<std::string> inverted_names;

        // global_atomic_prop holds all APs used in the automaton
        for (const auto& ap : *global_atomic_prop) {
            std::string ap_str = ap.ap_name();
            // If ap_str is NOT in controllable_var_names, add it to inverted_names
            if (std::find(controllable_var_names.begin(),
                          controllable_var_names.end(),
                          ap_str) == controllable_var_names.end()) {
                //std::cout << "Inverting variable: " << ap_str << std::endl;
                inverted_names.push_back(ap_str);
            }
        }

    return inverted_names;
}

StateSet
SolveAutEL(
    spot::twa_graph_ptr aut,
    std::vector<std::string> controllable_vars_names
)
{
    global_aut = &aut;
    global_atomic_prop = &(aut->ap());
    global_controllable_var_names = &controllable_vars_names;
    std::vector<std::string> inverted_controllable_var_names = get_inverted_variable_names(
        controllable_vars_names,
        aut->ap()
    );
    global_inverted_controllable_var_names = &inverted_controllable_var_names;

    auto mtdta = spot::dtwa_to_mtdtwa(aut);

    // Fix: dtwa_to_mtdtwa collapses accepting sinks into bddtrue and
    // bddfalse leaves, which bypass the terminal callback.
    // bdd_const_to_terminal expects terminal BDDs for both replacements,
    // so we create two sink states: one accepting, one rejecting.
    {
        // Accepting sink (replaces bddtrue)
        unsigned acc_sink_state = mtdta->states.size();
        unsigned acc_term_idx = mtdta->terminal_data_map.size();
        mtdta->terminal_data_map.push_back({{}, acc_sink_state});
        bdd acc_sink_term = bdd_terminal(acc_term_idx);
        mtdta->states.push_back(acc_sink_term); // self-loop
        global_accepting_sink_terminal = acc_term_idx;

        // Rejecting sink (replaces bddfalse)
        unsigned rej_sink_state = mtdta->states.size();
        unsigned rej_term_idx = mtdta->terminal_data_map.size();
        mtdta->terminal_data_map.push_back({{}, rej_sink_state});
        bdd rej_sink_term = bdd_terminal(rej_term_idx);
        mtdta->states.push_back(rej_sink_term); // self-loop
        global_rejecting_sink_terminal = rej_term_idx;

        // Replace bddfalse → rej_sink_term, bddtrue → acc_sink_term
        bddExtCache cache;
        bdd_extcache_init(&cache, mtdta->num_roots() * 2, false);
        for (auto& s : mtdta->states)
            s = bdd_const_to_terminal(s, rej_sink_term, acc_sink_term, &cache, 0);
        bdd_extcache_done(&cache);
    }

    global_mtdta = &mtdta;

    // Use the MTDTWA root count, not aut->num_states(), because
    // dtwa_to_mtdtwa may produce fewer roots than the automaton has
    // states (e.g. unreachable states are dropped).  A state that has
    // no corresponding MTDTWA root can never appear in an attractor
    // result, so including it in global_states would make the restart
    // condition in SolveAutEL_t permanently true → infinite loop.
    StateSet aut_states;
    for (unsigned i = 0; i < mtdta->num_roots(); ++i) {
        aut_states.insert(i);
    }
    global_states = &aut_states;
    ZlPtr zl_tree = new spot::zielonka_tree(aut->acc());
    global_accepting_player = zl_tree->is_even() ? 0 : 1;
    HierachicalChecker* hc = new HierachicalChecker();
    StateSet w_partition = {};
    StateSet l_partition = {};

    if (trace_enabled()) {
        std::cerr << CLR_BOLD "\n"
            "============================================================\n"
            "  SolveAutEL  —  Emerson-Lei Game Solver Trace\n"
            "============================================================\n"
            CLR_RESET;
        std::cerr << "States (V):       " << fmt_set(aut_states) << "\n";
        std::cerr << "Acceptance:       " << aut->acc() << "\n";
        std::cerr << "Controllable:     ";
        for (const auto& c : controllable_vars_names) std::cerr << c << " ";
        if (controllable_vars_names.empty()) std::cerr << "(none)";
        std::cerr << "\n";
        std::cerr << "Uncontrollable:   ";
        for (const auto& c : inverted_controllable_var_names) std::cerr << c << " ";
        std::cerr << "\n";
        std::cerr << "Zlk tree is_even: " << zl_tree->is_even()
                  << "  branches: " << zl_tree->num_branches()
                  << "  nodes: " << zl_tree->nodes_.size() << "\n";
        std::cerr << "------------------------------------------------------------\n\n";
        trace_depth = 0;
    }

    StateSet result = SolveAutEL_t(
        w_partition,
        l_partition,
        zl_tree,
        0,
        hc,
        zl_tree->is_even()
    );


    delete hc;
    delete zl_tree;
    return result;
}

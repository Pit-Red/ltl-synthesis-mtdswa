

#include "HierachicalChecker.hh"
#include "solve_el_aut_swa.hh"

#include <spot/twaalgos/zlktree.hh>
#include <spot/twaalgos/mtdswa.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/dot.hh>
#include <spot/tl/formula.hh>
#include <spot/twa/bdddict.hh>
#include <spot/twaalgos/determinize.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/graph/adjlist.hh>
#include <iostream>
#include <fstream>
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
using StateSetList = std::vector<StateSet>;
using Mark      = spot::acc_cond::mark_t;
using ZlNodePtr = spot::zielonka_tree::zielonka_node*;
using ZlPtr = spot::zielonka_tree*;


// =========================================================================
// Globals for BDD callback (same pattern as mtdtwa version)
// =========================================================================
static std::vector<bool>* global_is_winning = nullptr;
static std::vector<bool>* global_is_losing = nullptr;
static const HierachicalChecker* global_hierarchical_checker = nullptr;
static const spot::mtdswa_ptr* global_mtdswa = nullptr;
static const StateSet* global_states = nullptr; // V
static const std::vector<spot::formula>* global_atomic_prop = nullptr;
static const std::vector<std::string>* global_controllable_var_names = nullptr;
static const std::vector<std::string>* global_inverted_controllable_var_names = nullptr;
static const spot::twa_graph_ptr* global_aut = nullptr;
static int global_current_player = 0;
static std::vector<bool>* global_is_accepting_sink = nullptr;
static int global_accepting_player = 0; // player who wins from accepting plays
static const StateSet* global_excluded = nullptr; // states excluded from becoming winning


// alias for readability: map from mark/state → **set** of ints
using SetMap = std::unordered_map<int, std::unordered_set<int>>;

// Function to check if a terminal is winning.
// In mtdswa, the terminal value IS the destination state index.
// Sink states are handled without marks: an accepting sink is an
// absolute win for the accepting player, regardless of Zielonka context.
static int is_winning_terminal_with_acc(int v) {
    unsigned dst = v;

    // Accepting sink: always winning for the accepting player, always
    // losing for the other.  No marks needed — the play stays in the
    // sink forever and the acceptance outcome is fixed.
    // if ((*global_is_accepting_sink)[dst])
    //     return global_current_player == global_accepting_player;

    // If the destination state is excluded (already determined as losing),
    // it cannot be considered winning regardless of acceptance marks.
    
    bool is_excluded = global_excluded && global_excluded->count(dst) > 0;

    const auto& mark = (*global_mtdswa)->colors[dst];
    bool acc_win = global_hierarchical_checker->check(mark, global_current_player);
    if (acc_win)
        TRACE(CLR_DIM "    winning terminal: state=" << dst << " color=" << mark << CLR_RESET "\n");
    return (*global_is_winning)[dst] || (acc_win && !is_excluded);
}

// Function to estimate size for BDD cache
static size_t size_estimate_unary(const spot::mtdswa_ptr& mtdswa) {
    return mtdswa->num_roots() * 2;
}


// Helper function to get variable index from name
int get_var_index_swa(const spot::twa_graph_ptr& aut, const std::string& var_name) {
    spot::formula f = spot::formula::ap(var_name);
    return aut->get_dict()->register_proposition(f, aut.get());
}


// Function to convert vector of variable names to BDD
bdd variables_to_bdd_swa(const spot::twa_graph_ptr& aut, const std::vector<std::string>& var_names) {
    bdd result = bddtrue;
    for (const auto& var_name : var_names) {
        int var_index = get_var_index_swa(aut, var_name);
        result &= bdd_ithvar(var_index);
    }
    return result;
}


StateSet attractor_states_swa(
    const spot::mtdswa_ptr& mtdswa,
    const HierachicalChecker& hc,
    const std::vector<std::string>& controllable_var_names,
    const StateSet& seed,
    const StateSet& excluded,
    const int current_player
) {
    TRACE(CLR_DIM "attractor(" << fmt_player(current_player)
          << CLR_DIM << ", w_acc=" << fmt_marks(hc.get_ordered_marks())
          << ", seed=" << fmt_set(seed)
          << ", excluded=" << fmt_set(excluded) << ")" CLR_RESET "\n");

    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_unary(mtdswa), false);
    int iteration = 0;

    unsigned nroots = mtdswa->num_roots();
    std::vector<bool> winning(nroots, false);
    global_is_winning = &winning;
    global_hierarchical_checker = &hc;
    global_current_player = current_player;
    global_mtdswa = &mtdswa;
    global_excluded = &excluded;

    // Prepare for quantification over controllable variables
    bdd quantified_vars;

    if (current_player == 0)
        quantified_vars = variables_to_bdd_swa(*global_aut, *global_controllable_var_names);
    else
        quantified_vars = variables_to_bdd_swa(*global_aut, *global_inverted_controllable_var_names);

    bdd_mt_quantify_prepare(quantified_vars);

    for (unsigned i = 0; i < nroots; ++i) {
        if (seed.count(i) > 0)
            winning[i] = true;
    }

    // Pre-seed: root states whose color already satisfies the acceptance
    // condition (via the HierarchicalChecker) are immediately winning,
    // without needing the BDD fixed-point to discover them.
    for (unsigned i = 0; i < nroots; ++i) {
        if (winning[i])
            continue;
        // const auto& color = (*global_mtdswa)->colors[i];
        // if (hc.check(color, current_player)) {
        //     winning[i] = true;
        //     TRACE(CLR_DIM "  pre-seed: root state " << i
        //           << " already winning (color=" << color << ")" CLR_RESET "\n");
        // }
    }

    bool has_changed;
    do {
        has_changed = false;
        for (unsigned i = 0; i < nroots; ++i) {
            if (winning[i])
                continue;
            if (excluded.count(i) > 0)
                continue;
            bdd b = mtdswa->states[i];

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


bool equal_statesets_swa(const StateSet& a, const StateSet& b) {
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

StateSet union_statesets_swa(const StateSet& a, const StateSet& b) {
    StateSet result = a;
    for (const auto& elem : b) {
        result.insert(elem);
    }
    return result;
}

StateSet difference_statesets_swa(const StateSet& a, const StateSet& b) {
    StateSet result;
    for (const auto& elem : a) {
        if (b.find(elem) == b.end()) {
            result.insert(elem);
        }
    }
    return result;
}

StateSet
SolveAutEL_swa_t(
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

    TRACE(CLR_BOLD CLR_CYAN "\u250c\u2500 SolveAutEL_swa_t" CLR_RESET
          " zlk_node=" << zl_node_idx
          << " level=" << zl_node_parent->level
          << " colors=" << zl_node_parent->colors
          << " w_player=" << fmt_player(w_player)
          << " l_player=" << fmt_player(l_player) << "\n");
    TRACE("  W=" << fmt_set(w_partition)
          << "  L=" << fmt_set(l_partition)
          << "  V=" << fmt_set(*global_states) << "\n");
    ++trace_depth;

restart:
    {
        unsigned int starting_index = zl_node_parent->first_child;

        // Base case: leaf node — all non-losing states are winning for w_player
        if (starting_index == 0) {
            StateSet base_result = difference_statesets_swa(*global_states, l_partition);
            TRACE(CLR_YELLOW "\u2514 LEAF" CLR_RESET " -> return V\\L = "
                  << fmt_set(base_result) << "\n");
            --trace_depth;
            return base_result;
        }

        StateSet result = difference_statesets_swa(*global_states, l_partition);

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

            StateSetList W{1};
            W[0] = difference_statesets_swa(*global_states, l_partition);
            int j = 0;

            HierachicalChecker* hc_prime = hc->new_instance(zl_node_parent->colors.operator-(zl_node->colors), w_player);
            struct HcGuard { HierachicalChecker* p; ~HcGuard() { delete p; } } hc_prime_guard{hc_prime};

            TRACE(CLR_MAGENTA "  initial attractor for " << fmt_player(w_player)
                  << CLR_RESET "\n");
            W[0] = attractor_states_swa(
                    *global_mtdswa,
                    *hc_prime,
                    *global_controllable_var_names,
                    w_partition,
                    l_partition,  // excluded: states already marked as losing
                    w_player
                );
            TRACE("  W[0] = " << fmt_set(W[0]) << "\n");

            for (; j == 0 || !equal_statesets_swa(W[2*j], W[2*j-2]); j++){
                W.resize(2*j + 3);

                TRACE(CLR_BOLD "\u251c\u2500 fixed-point iter j=" << j << CLR_RESET
                      << "  W[" << 2*j << "]=" << fmt_set(W[2*j]) << "\n");

                W[2*j + 1] = W[2*j];

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

                    HierachicalChecker* hc_double_prime = hc_prime->new_instance(zl_node->colors.operator-(zl_child_node->colors));
                    struct HcGuard2 { HierachicalChecker* p; ~HcGuard2() { delete p; } } hc_double_prime_guard{hc_double_prime};

                    TRACE("    L-attractor for " << fmt_player(l_player) << "\n");
                    L = attractor_states_swa(
                        *global_mtdswa,
                        *hc_double_prime,
                        *global_controllable_var_names,
                        l_partition,
                        W[2*j],  // excluded: states already marked as winning
                        l_player  // switch player
                    );
                    TRACE("    L = " << fmt_set(L) << "\n");

                    TRACE("    " CLR_BOLD "RECURSE" CLR_RESET " into zlk_node="
                          << child_it_index << " with L="
                          << fmt_set(L) << "\n");
                    W[2*j + 1] = SolveAutEL_swa_t(
                        W[2*j],
                        L,
                        zl_tree,
                        child_it_index,
                        hc_double_prime,
                        is_even
                    );
                    TRACE("    returned W[" << 2*j+1 << "] = "
                          << fmt_set(W[2*j + 1]) << "\n");

                    if (!equal_statesets_swa(W[2*j + 1], W[2*j])) {
                        TRACE("    " CLR_RED "W shrunk!" CLR_RESET
                              " W[" << 2*j+1 << "] != W[" << 2*j
                              << "] -> break children loop\n");
                        break;
                    }

                    child_it_index = zl_child_node->next_sibling;
                    ++child_num;

                }while (child_it_index != starting_child_index);

                TRACE("  W-attractor for " << fmt_player(w_player) << "\n");
                W[2*j + 2] = attractor_states_swa(
                    *global_mtdswa,
                    *hc_prime,
                    *global_controllable_var_names,
                    W[2*j + 1],
                    l_partition,  // excluded: states already marked as losing
                    w_player
                );
                TRACE("  W[" << 2*j+2 << "] = " << fmt_set(W[2*j + 2]) << "\n");
            }

            TRACE("  fixed-point converged at j=" << j
                  << "  W[" << 2*j << "]=" << fmt_set(W[2*j]) << "\n");

            if (!equal_statesets_swa(union_statesets_swa(W[2*j], l_partition), *global_states)) {
                // Pseudocode: L_se = autattr_1(W_in, L_se ∪ (V\W_2j), f_t)
                StateSet new_l_seed = union_statesets_swa(l_partition, difference_statesets_swa(*global_states, W[2*j]));
                l_partition = attractor_states_swa(
                    *global_mtdswa,
                    *hc,
                    *global_controllable_var_names,
                    new_l_seed,
                    w_partition,  // excluded: states already marked as winning
                    l_player
                );
                TRACE(CLR_RED CLR_BOLD "  RESTART" CLR_RESET
                      " W[2j] \u222a L != V, growing l_partition\n");
                TRACE("  l_partition = " << fmt_set(l_partition)
                      << " -> goto restart\n");
                goto restart;
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

std::vector<std::string> get_inverted_variable_names_swa(
    const std::vector<std::string>& controllable_var_names,
    const std::vector<spot::formula>& atomic_prop
) {
    std::vector<std::string> inverted_names;

        for (const auto& ap : *global_atomic_prop) {
            std::string ap_str = ap.ap_name();
            if (std::find(controllable_var_names.begin(),
                          controllable_var_names.end(),
                          ap_str) == controllable_var_names.end()) {
                inverted_names.push_back(ap_str);
            }
        }

    return inverted_names;
}

StateSet
SolveAutEL_swa(
    spot::twa_graph_ptr aut,
    std::vector<std::string> controllable_vars_names
)
{
    // Convert to state-based acceptance if needed
    aut = spot::sbacc(aut);

    global_aut = &aut;
    global_atomic_prop = &(aut->ap());
    global_controllable_var_names = &controllable_vars_names;
    std::vector<std::string> inverted_controllable_var_names = get_inverted_variable_names_swa(
        controllable_vars_names,
        aut->ap()
    );
    global_inverted_controllable_var_names = &inverted_controllable_var_names;

    auto mtdswa = spot::dtwa_to_mtdswa(aut);

    // Convert bddtrue/bddfalse sinks into proper states with colors
    mtdswa->sinks_as_states();

    // Identify accepting sink states: self-loop terminal whose color
    // satisfies the acceptance condition.  These are the states that
    // dtwa_to_mtdswa originally collapsed into bddtrue.
    std::vector<bool> is_accepting_sink(mtdswa->num_roots(), false);
    for (unsigned s = 0; s < mtdswa->num_roots(); ++s) {
        if (!bdd_is_terminal(mtdswa->states[s]))
            continue;
        unsigned t = bdd_get_terminal(mtdswa->states[s]);
        if (t == s && mtdswa->acc.accepting(mtdswa->colors[s]))
            is_accepting_sink[s] = true;
    }
    global_is_accepting_sink = &is_accepting_sink;
    global_mtdswa = &mtdswa;

    StateSet aut_states;
    for (unsigned i = 0; i < mtdswa->num_roots(); ++i) {
        aut_states.insert(i);
    }
    global_states = &aut_states;
    ZlPtr zl_tree = new spot::zielonka_tree(aut->acc());
    // The accepting player is the one who benefits from the acceptance
    // condition being satisfied — determined by the Zielonka tree root parity.
    global_accepting_player = zl_tree->is_even() ? 0 : 1;
    HierachicalChecker* hc = new HierachicalChecker();
    StateSet w_partition = {};
    StateSet l_partition = {};

    if (trace_enabled()) {
        std::cerr << CLR_BOLD "\n"
            "============================================================\n"
            "  SolveAutEL_swa  —  Emerson-Lei Game Solver (mtdswa)\n"
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

    StateSet result = SolveAutEL_swa_t(
        w_partition,
        l_partition,
        zl_tree,
        0,
        hc,
        zl_tree->is_even()
    );

    // The algorithm returns w_player's winning set.  When is_even is
    // false, w_player is 1 (env), so we must complement to get player 0's
    // (sys) winning set, which is what the caller expects.
    if (!zl_tree->is_even())
        result = difference_statesets_swa(aut_states, result);

    delete hc;
    delete zl_tree;
    return result;
}

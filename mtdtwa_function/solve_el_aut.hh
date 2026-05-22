#ifndef SOLVE_EL_AUT_HH
#define SOLVE_EL_AUT_HH

#include <spot/twa/twagraph.hh>
#include <string>
#include <vector>
#include <unordered_set>

// --- Public alias for the winning set ---
using StateSet = std::unordered_set<spot::twa_graph::state_num>;

/**
 * SolveAutEL:
 *   Solve the Emerson–Lei game for a deterministic TWA.
 *
 * @param aut
 *      A deterministic TWA (spot::twa_graph_ptr)
 *
 * @param controllable_vars_names
 *      Names of atomic propositions controlled by player 0.
 *
 * @return
 *      The set of states from which player 0 wins.
 */
StateSet SolveAutEL(
    spot::twa_graph_ptr aut,
    std::vector<std::string> controllable_vars_names
);

#endif // SOLVE_EL_AUT_HH

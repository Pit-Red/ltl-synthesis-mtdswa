#ifndef SOLVE_EL_AUT_SWA_HH
#define SOLVE_EL_AUT_SWA_HH

#include <spot/twa/twagraph.hh>
#include <string>
#include <vector>
#include <unordered_set>

// --- Public alias for the winning set ---
using StateSet = std::unordered_set<spot::twa_graph::state_num>;

/**
 * SolveAutEL_swa:
 *   Solve the Emerson–Lei game for a deterministic TWA,
 *   using mtdswa (state-based acceptance) instead of mtdtwa.
 *
 *   This avoids the bug where dtwa_to_mtdtwa collapses accepting
 *   sink states into bddtrue, bypassing the terminal callback.
 *   With mtdswa + sinks_as_states(), sinks become proper states
 *   with acceptance colors that the HierachicalChecker can evaluate.
 *
 * @param aut
 *      A deterministic TWA (spot::twa_graph_ptr).
 *      Will be converted to state-based acceptance internally if needed.
 *
 * @param controllable_vars_names
 *      Names of atomic propositions controlled by player 0.
 *
 * @return
 *      The set of states from which player 0 wins.
 */
StateSet SolveAutEL_swa(
    spot::twa_graph_ptr aut,
    std::vector<std::string> controllable_vars_names
);

#endif // SOLVE_EL_AUT_SWA_HH

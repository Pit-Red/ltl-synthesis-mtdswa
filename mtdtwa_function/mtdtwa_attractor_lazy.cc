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
#include <../buddy/src/cache.h>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <utility>


// Global variables
static std::vector<bool>* global_is_winning = nullptr;
static const spot::acc_cond::mark_t* global_acc_set = nullptr;
static const spot::mtdtwa_ptr* global_mtdta = nullptr;

// alias for readability: map from mark/state → **set** of ints
using SetMap = std::unordered_map<int, std::unordered_set<int>>;

std::pair<SetMap, SetMap>
build_reachable_reverse_mtdta(const spot::mtdtwa_ptr& mtdta) {
    const unsigned n_roots = mtdta->num_roots();

    SetMap m_colors;
    SetMap m_states;

    // initialize empty sets for every state
    for (unsigned state = 0; state < n_roots; ++state) {
        m_states[state];  // default‐construct empty std::unordered_set<int>
    }

    for (unsigned src = 0; src < n_roots; ++src) {
        for (auto t : silent_paths_mt_of(mtdta->states[src])) {
            if (t == bddfalse) {
                continue;
            }
            else if (t == bddtrue) {
                std::cout << "src=" << src << "  →  [unconditional accept]\n";
                continue;
            }

            int idx = bdd_get_terminal(t); // get the terminal index
            auto [mark, dst] = mtdta->terminal_data[idx];

            // insert into the mark→src set
            for (auto m : mark.sets()) {
                m_colors[m].insert(src);
            }

            // insert into the state→dst set
            m_states[src].insert(dst);
        }
    }

    return {m_colors, m_states};
}

// Function to check if a terminal is winning
static int is_winning_terminal_with_acc(int v) {
    int dst = v;
    const auto& d = (*global_mtdta)->terminal_data[dst];
    return (((d.first & *global_acc_set) != spot::acc_cond::mark_t{}) || (*global_is_winning)[dst]);
}

// Function to estimate size for BDD cache
static size_t size_estimate_unary(const spot::mtdtwa_ptr& mtdta) {
    return mtdta->num_roots() * 2;
}


std::vector<bool>
mtdfa_winning_region_lazy(const spot::mtdtwa_ptr& mtdta, 
                          const spot::acc_cond::mark_t& acc_set, 
                          const bdd& controllable_vars)
{
  bddExtCache cache;
  bdd_extcache_init(&cache, size_estimate_unary(mtdta), false);

  auto [map_colors, map_states] = build_reachable_reverse_mtdta(mtdta);

  unsigned nroots = mtdta->num_roots();
  std::vector<bool> winning(nroots, false);
  std::vector<int> seen(nroots, -1); // last iteration seen
  global_is_winning = &winning;
  global_acc_set = &acc_set;
  global_mtdta = &mtdta;

  // The upcoming calls to bdd_mt_quantify_to_bool depend on this
  // setup.
  bdd_mt_quantify_prepare(controllable_vars);

  std::deque<unsigned> todo;
  // The first states in the todo will be the states that can
  // reach the acceptance set.  We will add them to the todo list

  std::unordered_set<int> initial_states;
  for (auto c : acc_set.sets()) {
      for (int s : map_colors[c]) {
          initial_states.insert(s);
      }
  }

  for (auto s : initial_states) {
    todo.push_back(s);
    seen[s] = 0;
  }

  std::deque<unsigned> changed;

  for (int iteration = 0; !todo.empty(); ++iteration)
    {
      do
        {
          unsigned i = todo.front();
          todo.pop_front();
          if (winning[i])
            continue;

          if (bdd_mt_quantify_to_bool(mtdta->states[i],
                                      is_winning_terminal_with_acc,
                                      &cache, iteration))
            {
              // By modifying winning, we modify the behavior of
              // is_winning_terminal_lazy.  That should normally
              // call for an invalidation of the cache (or
              // equivalently, an increment of the iteration
              // number), but it is actually OK if the cache uses
              // previous values, as if winning was constant during
              // one iteration.  The new values are sure to be used
              // on next iteration.
              winning[i] = true;
              // if the initial state is winning, we can stop
              if (i == 0) // IDK if it still makes sense
                goto done;
              changed.push_back(i);
            }
        }
      while (!todo.empty());
      // Schedule non-winning predecessors for next iteration.
      for (unsigned i: changed){
        for (unsigned p: map_states[i]){
          if (!winning[p] && seen[p] != iteration)
            {
              seen[p] = iteration;
              std::cout << "pushed " << p << std::endl;
              todo.push_front(p); // check if this part works correctly
            }
        }
      }
      changed.clear();
    }
  done:
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


int main(int argc, char* argv[])
{
    if (argc < 4 || argc > 5) {
        std::cerr << "Usage: " << argv[0]
                  << " \"<formula>\" <acc_set_comma_sep> <controllable_vars_comma_sep> [<png-output>]\n"
                  << "Example (just JSON):\n"
                  << "  " << argv[0]
                  << " \"G(F a U b) <-> FG c\" 1 b\n\n"
                  << "Example (and generate PNG):\n"
                  << "  " << argv[0]
                  << " \"G(F a U b) <-> FG c\" 1 b attractor.png\n";
        return 1;
    }

    // Parsing the formula
    std::string formula_str = argv[1];
    spot::formula f = spot::parse_formula(formula_str);

    // Building a deterministic complete TWA
    spot::translator trans;
    trans.set_type(spot::postprocessor::Generic);
    trans.set_pref(spot::postprocessor::Deterministic |
                  spot::postprocessor::Complete);
    spot::twa_graph_ptr aut = trans.run(f);
    auto mtdta = spot::dtwa_to_mtdtwa(aut);

    // If the user provided a PNG output path, dump DOT and run 'dot'
    if (argc == 5) {
        const char* png_path = argv[4];
        const std::string dot_path = "bdd.dot";

        // Printing the DOT
        std::ofstream dot_file(dot_path);
        if (!dot_file) {
            std::cerr << "Error: cannot open " << dot_path << " for writing\n";
            return 1;
        }
        mtdta->print_dot(dot_file);
        dot_file.close();

        // Invoking Graphviz to make the PNG
        std::string cmd = "dot -Tpng " + dot_path + " -o " + png_path;
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "Error: 'dot' command failed (exit code " << ret << ")\n";
            // fall through — we still print the JSON below
        } else {
            std::cerr << "Wrote attractor diagram to " << png_path << "\n";
        }

        // remove temporary DOT
        std::remove(dot_path.c_str());
    }

    // Parsing the acceptance set
    spot::acc_cond::mark_t acc_set;
    {
        std::istringstream ss(argv[2]);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            int m = std::stoi(tok);
            acc_set.set(m);
        }
    }
    std::cerr << "Using acceptance marks: " << acc_set << "\n";

    // Parsing controllable variable names
    std::vector<std::string> controllable_vars_names;
    {
        std::istringstream ss(argv[3]);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            controllable_vars_names.push_back(tok);
        }
    }
    bdd controllable_vars = variables_to_bdd(aut, controllable_vars_names);

    // Computing the lazy attractor
    auto winning =
      mtdfa_winning_region_lazy(mtdta, acc_set, controllable_vars);

    // Emitting JSON array of winning flags on stdout
    std::cout << '[';
    for (size_t i = 0; i < winning.size(); ++i) {
        std::cout << (winning[i] ? "true" : "false");
        if (i + 1 < winning.size())
            std::cout << ',';
    }
    std::cout << "]\n";
    return 0;
}
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

struct RandLtlOptions {
    // Either provide atomic_props OR ap_count.
    std::vector<std::string> atomic_props;
    std::optional<int> ap_count;

    // Generation parameters
    int seed = 0;                  // randltl default seed is 0
    int formulas = 1;              // default: 1 formula
    std::string tree_size = "15";  // default tree size is 15
    bool allow_dups = false;
    std::optional<int> simplify_level; // 0..3
    bool weak_fairness = false;

    // Example: "xor=0,implies=0,equiv=0,X=10"
    std::optional<std::string> ltl_priorities;

    // Output style
    enum class Syntax {
        Spot,
        Spin,
        Wring,
        LBT
    } syntax = Syntax::Spot;

    bool full_parentheses = false;
};

/*
* Generates random LTL formulas using the randltl tool with the specified options.
* Returns either a single formula (if opts.formulas == 1) or the full output from randltl.
* Throws std::runtime_error if randltl fails or produces no formula.
*/
std::vector<std::string> generate_random_ltl(const RandLtlOptions& opts);
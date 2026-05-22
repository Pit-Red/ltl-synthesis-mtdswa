#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct SynthesisResult {
    std::string formula;
    bool realizable;
    std::string controller; // empty if unrealizable or if realizability_only=true
    std::string raw_output; // useful for debugging
};

struct LtlSyntOptions {
    std::vector<std::string> inputs;   // --ins=a,b,c
    std::vector<std::string> outputs;  // --outs=x,y,z

    bool realizability_only = false;   // --realizability
    bool hide_status = false;          // --hide-status
    bool aiger = false;                // --aiger
    bool dot = false;                  // --dot

    std::optional<std::string> algo;       // e.g. "acd"
    std::optional<std::string> simplify;   // e.g. "bwoa"
};

/*
 * Solves the LTL synthesis problem for a single formula.
 */
SynthesisResult solve_with_ltlsynt(
    const std::string& formula,
    const LtlSyntOptions& opts
);

/*
 * Solves the LTL synthesis problem for a list of formulas.
 */
std::vector<SynthesisResult> solve_with_ltlsynt(
    const std::vector<std::string>& formulas,
    const LtlSyntOptions& opts
);
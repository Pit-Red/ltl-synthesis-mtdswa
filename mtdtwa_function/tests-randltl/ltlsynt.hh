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
    double internal_time_ms = -1; // ltlsynt's self-reported total_time (excludes process overhead)
    bool error = false;
    std::string error_msg;
};

struct LtlSyntOptions {
    std::vector<std::string> inputs;   // --ins=a,b,c
    std::vector<std::string> outputs;  // --outs=x,y,z

    bool realizability_only = true;   // --realizability
    bool hide_status = false;          // --hide-status
    bool aiger = false;                // --aiger
    bool dot = false;                  // --dot

    std::optional<std::string> algo;       // e.g. "acd"
    std::optional<std::string> simplify;   // e.g. "bwoa"

    std::optional<std::string> bypass;     // "yes" or "no" (default: yes)
    std::optional<std::string> decompose;  // "yes" or "no" (default: yes)
    std::optional<std::string> splittype;  // "expl", "semisym", "fullysym", "auto"

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
#include "ltlsynt.hh"

static std::string join_comma(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += items[i];
    }
    return out;
}

static std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static std::string read_all_from_fd(int fd) {
    std::string output;
    char buffer[4096];

    while (true) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            output.append(buffer, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else {
            throw std::runtime_error("read() failed while reading ltlsynt output.");
        }
    }

    return output;
}

static std::string run_ltlsynt_single_formula(const std::string& formula,
                                              const LtlSyntOptions& opts) {
    if (formula.empty()) {
        throw std::invalid_argument("formula must not be empty.");
    }

    std::vector<std::string> args;
    args.emplace_back("ltlsynt");

    if (!opts.inputs.empty()) {
        args.emplace_back("--ins=" + join_comma(opts.inputs));
    }

    if (!opts.outputs.empty()) {
        args.emplace_back("--outs=" + join_comma(opts.outputs));
    }

    if (opts.realizability_only) {
        args.emplace_back("--realizability");
    }

    if (opts.hide_status) {
        args.emplace_back("--hide-status");
    }

    if (opts.aiger) {
        args.emplace_back("--aiger");
    }

    if (opts.dot) {
        args.emplace_back("--dot");
    }

    if (opts.algo.has_value()) {
        args.emplace_back("--algo=" + *opts.algo);
    }

    if (opts.simplify.has_value()) {
        args.emplace_back("--simplify=" + *opts.simplify);
    }

    args.emplace_back("-f");
    args.emplace_back(formula);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& s : args) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        throw std::runtime_error("pipe() failed.");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        throw std::runtime_error("fork() failed.");
    }

    if (pid == 0) {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            _exit(127);
        }
        if (dup2(pipefd[1], STDERR_FILENO) == -1) {
            _exit(127);
        }
        close(pipefd[1]);

        execvp("ltlsynt", argv.data());
        _exit(127);
    }

    close(pipefd[1]);

    std::string output;
    try {
        output = read_all_from_fd(pipefd[0]);
    } catch (...) {
        close(pipefd[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        throw;
    }

    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        throw std::runtime_error("waitpid() failed.");
    }

    if (!WIFEXITED(status)) {
        throw std::runtime_error("ltlsynt terminated abnormally.\nOutput:\n" + output);
    }

    int exit_code = WEXITSTATUS(status);

    // ltlsynt semantics:
    // 0 = realizable
    // 1 = unrealizable
    // 2 = actual tool error
    if (exit_code == 2) {
        throw std::runtime_error("ltlsynt reported an error.\nOutput:\n" + output);
    }

    // exit_code 0 or 1 are both valid synthesis outcomes

    return trim(output);
}

static SynthesisResult parse_ltlsynt_output(const std::string& formula,
                                            const std::string& output,
                                            bool realizability_only) {
    SynthesisResult result;
    result.formula = formula;
    result.raw_output = output;
    result.realizable = false;
    result.controller.clear();

    const std::string trimmed = trim(output);

    if (trimmed == "REALIZABLE") {
        result.realizable = true;
        return result;
    }

    if (trimmed == "UNREALIZABLE") {
        result.realizable = false;
        return result;
    }

    const std::string realizable_prefix = "REALIZABLE";
    const std::string unrealizable_prefix = "UNREALIZABLE";

    if (trimmed.rfind(realizable_prefix, 0) == 0) {
        result.realizable = true;

        if (!realizability_only) {
            std::string rest = trim(trimmed.substr(realizable_prefix.size()));
            result.controller = rest;
        }
        return result;
    }

    if (trimmed.rfind(unrealizable_prefix, 0) == 0) {
        result.realizable = false;
        result.controller.clear();
        return result;
    }

    throw std::runtime_error(
        "Could not parse ltlsynt output for formula: " + formula + "\nOutput:\n" + output
    );
}

SynthesisResult solve_with_ltlsynt(
    const std::string& formula,
    const LtlSyntOptions& opts
) {
    if (opts.hide_status) {
        throw std::invalid_argument(
            "hide_status=true is not supported in this wrapper because parsing "
            "REALIZABLE/UNREALIZABLE would become ambiguous."
        );
    }
    const std::string raw = run_ltlsynt_single_formula(formula, opts);
    return parse_ltlsynt_output(formula, raw, opts.realizability_only);
}

std::vector<SynthesisResult> solve_with_ltlsynt(
    const std::vector<std::string>& formulas,
    const LtlSyntOptions& opts
) {
    if (formulas.empty()) {
        throw std::invalid_argument("formulas must not be empty.");
    }

    // hide_status makes parsing impossible/reliable only with extra assumptions
    if (opts.hide_status) {
        throw std::invalid_argument(
            "hide_status=true is not supported in this wrapper because parsing "
            "REALIZABLE/UNREALIZABLE would become ambiguous."
        );
    }

    std::vector<SynthesisResult> results;
    results.reserve(formulas.size());

    for (const auto& formula : formulas) {
        const std::string raw = run_ltlsynt_single_formula(formula, opts);
        results.push_back(parse_ltlsynt_output(formula, raw, opts.realizability_only));
    }

    return results;
}

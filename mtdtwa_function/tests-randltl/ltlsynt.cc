#include "ltlsynt.hh"
#include <sstream>

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

// Parses ltlsynt --csv output to extract total_time (in seconds), returns ms.
// CSV format: header line, then data line with total_time at column index 4.
static double parse_csv_total_time(const std::string& csv_output) {
    // Find the data line (second line)
    auto nl = csv_output.find('\n');
    if (nl == std::string::npos) return -1;
    std::string data_line = trim(csv_output.substr(nl + 1));
    if (data_line.empty()) return -1;

    // total_time is the 5th field (index 4)
    std::istringstream iss(data_line);
    std::string field;
    for (int i = 0; i <= 4; ++i) {
        if (!std::getline(iss, field, ',')) return -1;
    }
    try {
        return std::stod(field) * 1000.0; // seconds -> ms
    } catch (...) {
        return -1;
    }
}

struct RawLtlSyntOutput {
    std::string stdout_text;
    std::string stderr_text;
    bool error = false;
    std::string error_msg;
};

static RawLtlSyntOutput run_ltlsynt_single_formula(const std::string& formula,
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

    if (opts.bypass.has_value()) {
        args.emplace_back("--bypass=" + *opts.bypass);
    }

    if (opts.decompose.has_value()) {
        args.emplace_back("--decompose=" + *opts.decompose);
    }

    if (opts.splittype.has_value()) {
        args.emplace_back("--splittype=" + *opts.splittype);
    }

    // Request CSV timing output on stderr
    args.emplace_back("--csv=/dev/stderr");

    args.emplace_back("-f");
    args.emplace_back(formula);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& s : args) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) == -1) {
        throw std::runtime_error("pipe() failed for stdout.");
    }
    if (pipe(stderr_pipe) == -1) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        throw std::runtime_error("pipe() failed for stderr.");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        throw std::runtime_error("fork() failed.");
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        if (dup2(stdout_pipe[1], STDOUT_FILENO) == -1) _exit(127);
        if (dup2(stderr_pipe[1], STDERR_FILENO) == -1) _exit(127);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execvp("ltlsynt", argv.data());
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    RawLtlSyntOutput raw;

    try {
        raw.stdout_text = read_all_from_fd(stdout_pipe[0]);
        raw.stderr_text = read_all_from_fd(stderr_pipe[0]);
    } catch (...) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        throw;
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        throw std::runtime_error("waitpid() failed.");
    }

    if (!WIFEXITED(status)) {
        raw.error = true;
        raw.error_msg = "ltlsynt terminated by signal.\nStderr:\n" + raw.stderr_text;
        return raw;
    }

    int exit_code = WEXITSTATUS(status);
    if (exit_code == 2) {
        raw.error = true;
        raw.error_msg = "ltlsynt reported an error.\nStderr:\n" + raw.stderr_text;
        return raw;
    }

    raw.stdout_text = trim(raw.stdout_text);
    return raw;
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
    RawLtlSyntOutput raw = run_ltlsynt_single_formula(formula, opts);
    if (raw.error) {
        SynthesisResult result;
        result.formula = formula;
        result.error = true;
        result.error_msg = raw.error_msg;
        return result;
    }
    SynthesisResult result = parse_ltlsynt_output(formula, raw.stdout_text, opts.realizability_only);
    result.internal_time_ms = parse_csv_total_time(raw.stderr_text);
    return result;
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
        RawLtlSyntOutput raw = run_ltlsynt_single_formula(formula, opts);
        if (raw.error) {
            SynthesisResult result;
            result.formula = formula;
            result.error = true;
            result.error_msg = raw.error_msg;
            results.push_back(std::move(result));
            continue;
        }
        SynthesisResult result = parse_ltlsynt_output(formula, raw.stdout_text, opts.realizability_only);
        result.internal_time_ms = parse_csv_total_time(raw.stderr_text);
        results.push_back(std::move(result));
    }

    return results;
}

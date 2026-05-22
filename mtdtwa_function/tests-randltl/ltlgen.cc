#include "ltlgen.hh"

static std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> generate_random_ltl(const RandLtlOptions& opts) {
    if (opts.atomic_props.empty() && !opts.ap_count.has_value()) {
        throw std::invalid_argument(
            "You must provide either atomic_props or ap_count."
        );
    }

    if (!opts.atomic_props.empty() && opts.ap_count.has_value()) {
        throw std::invalid_argument(
            "Provide either atomic_props or ap_count, not both."
        );
    }

    if (opts.formulas <= 0) {
        throw std::invalid_argument("formulas must be > 0 for this function.");
    }

    // Build argv for execvp()
    std::vector<std::string> args;
    args.emplace_back("randltl");

    args.emplace_back("--seed=" + std::to_string(opts.seed));
    args.emplace_back("-n" + std::to_string(opts.formulas));
    args.emplace_back("--tree-size=" + opts.tree_size);

    if (opts.allow_dups) {
        args.emplace_back("--allow-dups");
    }

    if (opts.simplify_level.has_value()) {
        args.emplace_back("-r" + std::to_string(*opts.simplify_level));
    }

    if (opts.weak_fairness) {
        args.emplace_back("--weak-fairness");
    }

    if (opts.ltl_priorities.has_value() && !opts.ltl_priorities->empty()) {
        args.emplace_back("--ltl-priorities=" + *opts.ltl_priorities);
    }

    if (opts.full_parentheses) {
        args.emplace_back("-p");
    }

    switch (opts.syntax) {
        case RandLtlOptions::Syntax::Spot:
            args.emplace_back("--spot");
            break;
        case RandLtlOptions::Syntax::Spin:
            args.emplace_back("--spin");
            break;
        case RandLtlOptions::Syntax::Wring:
            args.emplace_back("--wring");
            break;
        case RandLtlOptions::Syntax::LBT:
            args.emplace_back("--lbt");
            break;
    }

    if (opts.ap_count.has_value()) {
        args.emplace_back(std::to_string(*opts.ap_count));
    } else {
        for (const auto& ap : opts.atomic_props) {
            args.emplace_back(ap);
        }
    }

    // Convert to char*[] for execvp
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& s : args) {
        argv.push_back(s.data());
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
        // Child: send stdout/stderr to pipe
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execvp("randltl", argv.data());

        // execvp only returns on failure
        _exit(127);
    }

    // Parent
    close(pipefd[1]);

    std::string output;
    char buffer[4096];
    ssize_t count;
    while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<size_t>(count));
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error(
            "randltl failed with output:\n" + output
        );
    }

    output = trim(output);

    // If one formula was requested, return the first non-empty line.
    if (opts.formulas == 1) {
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            line = trim(line);
            if (!line.empty()) {
                return {line};
            }
        }
        throw std::runtime_error("randltl produced no formula.");
    }

    // If multiple formulas were requested, return the whole output.
    std::vector<std::string> formulas;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (!line.empty()) {
            formulas.push_back(line);
        }
    }
    return formulas;
}

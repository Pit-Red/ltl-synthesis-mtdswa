#include <spot/twaalgos/zlktree.hh>
#include <spot/twa/twagraph.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>
#include <iostream>
#include <fstream>

// Helper: print a zielonka_node
void print_node(unsigned idx, const spot::zielonka_tree::zielonka_node& n)
{
    std::cout << "  node[" << idx << "]: "
              << "parent=" << n.parent
              << "  first_child=" << n.first_child
              << "  next_sibling=" << n.next_sibling
              << "  level=" << n.level
              << "  colors=" << n.colors
              << "\n";
}

// Helper: recursively print the tree structure
void print_tree(const spot::zielonka_tree& zt, unsigned idx, int depth)
{
    const auto& n = zt.nodes_[idx];
    for (int i = 0; i < depth; ++i) std::cout << "  ";
    std::cout << "[" << idx << "] level=" << n.level
              << " colors=" << n.colors << "\n";

    unsigned child = n.first_child;
    if (child != 0) {
        unsigned c = child;
        do {
            print_tree(zt, c, depth + 1);
            c = zt.nodes_[c].next_sibling;
        } while (c != child);
    }
}

void test_from_acc_cond(const std::string& name, const spot::acc_cond& acc)
{
    std::cout << "\n========== " << name << " ==========\n";
    std::cout << "Acceptance: " << acc << "\n";

    spot::zielonka_tree zt(acc);

    std::cout << "is_even:          " << zt.is_even() << "\n";
    std::cout << "num_branches:     " << zt.num_branches() << "\n";
    std::cout << "first_branch:     " << zt.first_branch() << "\n";
    std::cout << "has_rabin_shape:  " << zt.has_rabin_shape() << "\n";
    std::cout << "has_streett_shape:" << zt.has_streett_shape() << "\n";
    std::cout << "has_parity_shape: " << zt.has_parity_shape() << "\n";

    std::cout << "\nAll nodes (" << zt.nodes_.size() << "):\n";
    for (unsigned i = 0; i < zt.nodes_.size(); ++i)
        print_node(i, zt.nodes_[i]);

    std::cout << "\nTree structure:\n";
    print_tree(zt, 0, 0);

    // Demonstrate step() — walk through the tree with different color sets
    std::cout << "\nstepping from first_branch with various color sets:\n";
    unsigned branch = zt.first_branch();

    // Step with empty colors
    {
        auto [new_branch, level] = zt.step(branch, {});
        std::cout << "  step(branch=" << branch << ", colors={})  -> branch="
                  << new_branch << " level=" << level << "\n";
    }

    // Step with each individual color
    for (unsigned c = 0; c < acc.num_sets(); ++c)
    {
        spot::acc_cond::mark_t mark = {c};
        auto [new_branch, level] = zt.step(branch, mark);
        std::cout << "  step(branch=" << branch << ", colors={" << c << "}) -> branch="
                  << new_branch << " level=" << level << "\n";
    }

    // Step with all colors at once
    {
        spot::acc_cond::mark_t all = {};
        for (unsigned c = 0; c < acc.num_sets(); ++c)
            all.set(c);
        auto [new_branch, level] = zt.step(branch, all);
        std::cout << "  step(branch=" << branch << ", colors=all) -> branch="
                  << new_branch << " level=" << level << "\n";
    }

    // Export DOT
    std::string dot_path = "img/zlk_" + name + ".dot";
    std::string png_path = "img/zlk_" + name + ".png";
    std::ofstream dot_out(dot_path);
    if (dot_out) {
        zt.dot(dot_out);
        dot_out.close();
        std::string cmd = "dot -Tpng " + dot_path + " -o " + png_path;
        if (std::system(cmd.c_str()) == 0)
            std::cout << "Saved: " << png_path << "\n";
        std::remove(dot_path.c_str());
    }
}

int main()
{
    // 1) Büchi acceptance: Inf(0)
    //    Simplest case — one acceptance set
    test_from_acc_cond("buchi", spot::acc_cond("Inf(0)"));

    // 2) co-Büchi acceptance: Fin(0)
    test_from_acc_cond("cobuchi", spot::acc_cond("Fin(0)"));

    // 3) Rabin 1: Fin(0) & Inf(1)
    //    One Rabin pair — should have Rabin shape
    test_from_acc_cond("rabin1", spot::acc_cond("Fin(0) & Inf(1)"));

    // 4) Rabin 2: (Fin(0) & Inf(1)) | (Fin(2) & Inf(3))
    //    Two Rabin pairs — still Rabin shape
    test_from_acc_cond("rabin2", spot::acc_cond("Rabin 2"));

    // 5) Streett 1: Fin(0) | Inf(1)
    //    One Streett pair — should have Streett shape
    test_from_acc_cond("streett1", spot::acc_cond("Fin(0) | Inf(1)"));

    // 6) Parity min even 3: Inf(0) | (Fin(1) & Inf(2))
    //    Should have parity shape (both Rabin and Streett)
    test_from_acc_cond("parity3", spot::acc_cond("parity min even 3"));

    // 7) Generalized Büchi 2: Inf(0) & Inf(1)
    //    Not Rabin, not Streett, not parity
    test_from_acc_cond("genbuchi2", spot::acc_cond("Inf(0) & Inf(1)"));

    // 8) Mixed Emerson-Lei: Inf(0) | Fin(1) & Inf(2)
    //    Complex acceptance — see how the tree looks
    test_from_acc_cond("emerson_lei", spot::acc_cond("Inf(0) | (Fin(1) & Inf(2))"));

    // 9) Trivial true/false
    test_from_acc_cond("true", spot::acc_cond("t"));
    test_from_acc_cond("false", spot::acc_cond("f"));

    // 10) Multi-step walk: simulate reading a sequence of color sets
    std::cout << "\n========== Multi-step walk (Rabin 2) ==========\n";
    {
        spot::acc_cond acc("Rabin 2");
        spot::zielonka_tree zt(acc);
        unsigned branch = zt.first_branch();

        // Simulate a sequence of transitions with different acceptance marks
        std::vector<spot::acc_cond::mark_t> color_sequence = {
            {1},       // see Inf(1)
            {0, 1},    // see both Fin(0) and Inf(1)
            {},        // no marks
            {3},       // see Inf(3)
            {2, 3},    // see Fin(2) and Inf(3)
        };

        for (unsigned i = 0; i < color_sequence.size(); ++i) {
            auto [new_branch, level] = zt.step(branch, color_sequence[i]);
            std::cout << "  step " << i << ": branch=" << branch
                      << " + colors=" << color_sequence[i]
                      << " -> branch=" << new_branch
                      << " level=" << level << "\n";
            branch = new_branch;
        }
    }

    // 11) NO_EMPTY_LAYER option
    std::cout << "\n========== NO_EMPTY_LAYER option ==========\n";
    {
        spot::acc_cond acc("Inf(0)");
        spot::zielonka_tree zt_normal(acc);
        spot::zielonka_tree zt_no_empty(acc, spot::zielonka_tree_options::NO_EMPTY_LAYER);

        std::cout << "Normal:        nodes=" << zt_normal.nodes_.size()
                  << " branches=" << zt_normal.num_branches()
                  << " first_branch=" << zt_normal.first_branch() << "\n";
        std::cout << "NO_EMPTY_LAYER: nodes=" << zt_no_empty.nodes_.size()
                  << " branches=" << zt_no_empty.num_branches()
                  << " first_branch=" << zt_no_empty.first_branch()
                  << " empty_layer_is_even=" << zt_no_empty.empty_layer_is_even() << "\n";
    }

    // 12) MERGE_SUBTREES option — big difference for Rabin N
    std::cout << "\n========== MERGE_SUBTREES option ==========\n";
    {
        spot::acc_cond acc("Rabin 4");
        spot::zielonka_tree zt_normal(acc);
        spot::zielonka_tree zt_merged(acc, spot::zielonka_tree_options::MERGE_SUBTREES);

        std::cout << "Rabin 4 normal:  branches=" << zt_normal.num_branches()
                  << " nodes=" << zt_normal.nodes_.size() << "\n";
        std::cout << "Rabin 4 merged:  branches=" << zt_merged.num_branches()
                  << " nodes=" << zt_merged.nodes_.size() << "\n";
    }

    std::cout << "\nDone.\n";
    return 0;
}

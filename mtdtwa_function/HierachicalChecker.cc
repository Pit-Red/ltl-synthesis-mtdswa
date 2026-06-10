#include "HierachicalChecker.hh"
#include <iostream>

// Default constructor: empty set
HierachicalChecker::HierachicalChecker()
{

}

// Constructor from existing set
HierachicalChecker::HierachicalChecker(const OrderedMarkList& initial_marks)
    : ordered_marks_(initial_marks)
{

}

HierachicalChecker* HierachicalChecker::new_instance(const spot::acc_cond::mark_t& mark, int starting_player)
{
    // Copy the current ordered_marks_
    OrderedMarkList new_marks = ordered_marks_;

    // Append the new mark: alternate players, using starting_player for the first entry
    int player = new_marks.empty() ? starting_player : (new_marks.rbegin()->second == 0 ? 1 : 0);
    new_marks.push_back(std::make_pair(mark, player));

    // Create a new HierachicalChecker with the updated list
    return new HierachicalChecker(new_marks);
}

// Hierarchical check
bool HierachicalChecker::check(const spot::acc_cond::mark_t& mark, int curr_player) const
{
    if (mark == spot::acc_cond::mark_t{}) {
        return false; // empty mark always fails
    }

    for (const auto& mp : ordered_marks_) {
        const auto& stored_mark = mp.first;
        int stored_value = mp.second;


        auto interception = (mark & stored_mark); // posssible fixing
        if (interception.count() > 0) {
            if (stored_value != curr_player) {
                return false; // check fails
            } else {
                return true; // check passes
            }
        }
    }
    return false;
}

// Print all ordered pairs
void HierachicalChecker::print() const
{
    for (const auto& mp : ordered_marks_) {
        std::cout << "mark: " << mp.first << ", value: " << mp.second << "\n";
    }
}

// Remove all elements
void HierachicalChecker::clear()
{
    ordered_marks_.clear();
}


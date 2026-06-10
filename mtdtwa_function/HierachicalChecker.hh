#ifndef HIERACHICALCHECKER_HH
#define HIERACHICALCHECKER_HH

#include <spot/twa/acc.hh>
#include <vector>
#include <utility>
#include <initializer_list>

using MarkPair = std::pair<spot::acc_cond::mark_t, int>;
using OrderedMarkList = std::vector<MarkPair>;

class HierachicalChecker {
public:
    HierachicalChecker();                                              // default constructor
    HierachicalChecker(const OrderedMarkList& initial_marks);           // constructor from set

    bool check(const spot::acc_cond::mark_t& mark, int curr_player) const;
    HierachicalChecker* new_instance(const spot::acc_cond::mark_t& mark, int starting_player = 0);
    OrderedMarkList get_ordered_marks() const { return ordered_marks_; }
    void print() const;
    void clear();

private:
    OrderedMarkList ordered_marks_;
};

#endif
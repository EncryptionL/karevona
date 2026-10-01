// Generic validated state machine. A TransitionTable is immutable shared
// data describing which transitions are legal; a StateMachine is one
// instance walking that table. Not thread-safe: owners serialize access.
#pragma once

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "karevona/common.hpp"

namespace karevona {

template <class State>
class TransitionTable {
public:
    using Edge = std::pair<State, State>;

    TransitionTable(std::string name, std::vector<Edge> edges, std::function<const char*(State)> namer)
        : name_(std::move(name)), edges_(edges.begin(), edges.end()), namer_(std::move(namer)) {}

    bool allows(State from, State to) const { return edges_.count({from, to}) != 0; }

    bool is_terminal(State s) const {
        return std::none_of(edges_.begin(), edges_.end(), [&](const Edge& e) { return e.first == s; });
    }

    std::vector<State> successors(State from) const {
        std::vector<State> out;
        for (const auto& e : edges_) {
            if (e.first == from) out.push_back(e.second);
        }
        return out;
    }

    const std::string& name() const { return name_; }
    const char* state_name(State s) const { return namer_(s); }

private:
    std::string name_;
    std::set<Edge> edges_;
    std::function<const char*(State)> namer_;
};

template <class State>
class StateMachine {
public:
    StateMachine(const TransitionTable<State>& table, State initial) : table_(&table), state_(initial) {
        history_.push_back(initial);
    }

    State state() const { return state_; }
    bool is_terminal() const { return table_->is_terminal(state_); }
    bool can_transition(State to) const { return table_->allows(state_, to); }
    const std::vector<State>& history() const { return history_; }

    Status transition(State to) {
        if (!table_->allows(state_, to)) {
            return Status(ErrorCode::FailedPrecondition, table_->name() + ": illegal transition " +
                                                             table_->state_name(state_) + " -> " +
                                                             table_->state_name(to));
        }
        state_ = to;
        history_.push_back(to);
        return Status::ok();
    }

private:
    const TransitionTable<State>* table_;
    State state_;
    std::vector<State> history_;
};

}  // namespace karevona

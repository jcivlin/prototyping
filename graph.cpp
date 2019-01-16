#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cassert>

struct Branch;

// Nodes of the parse graph.
struct State {
    State(const std::string& name) : m_name(name) {}

    std::string m_name;
    std::vector<std::shared_ptr<Branch>> m_branches;
};

struct Branch {
    Branch(const std::shared_ptr<State>& state) : m_nextState(state) {}

    std::shared_ptr<State> m_nextState;
};

// Encodes a parse graph in the readable form that is easy to modify.
typedef std::unordered_map<std::string, std::vector<std::string>> StateToBranchedStates;

StateToBranchedStates ParseGraph = {
    {"start", {"start_loop", "s1"}},
    {"start_loop", {"loop_1", "s1"}},
    {"loop_1", {"loop_2"}},
    {"loop_2", {"start_loop", "accept"}},
    {"s1", {"accept"}},
    {"accept", {}}
};

// For testing a dead loop without an exit.
/*StateToBranchedStates ParseGraph = {
    {"start", {"start_loop", "s1"}},
    {"start_loop", {"loop_1"}},
    {"loop_1", {"loop_2"}},
    {"loop_2", {"start_loop"}},
    {"s1", {"accept"}},
    {"accept", {}}
};*/

// For testing crossing loops.
/*StateToBranchedStates ParseGraph = {
    {"start", {"s1", "s2"}},
    {"s1", {"s3"}},
    {"s2", {"s3"}},
    {"s3", {"s1", "s2", "accept"}},
    {"accept", {}}
};*/

// Builds a parse tree from its readable form.
std::shared_ptr<State> createGraph(const StateToBranchedStates& graph) {
    std::unordered_map<std::string, std::shared_ptr<State>> namesToStates;

    for (auto& elem : graph) {
        const std::string& stateName = elem.first;
        auto res = namesToStates.emplace(stateName, std::make_shared<State>(stateName));
        if (!res.second) {
            std::cerr << stateName << ": Multiple states with same name in parse graph." << std::endl;
            return std::shared_ptr<State>();
        }
    }

    for (auto& elem : graph) {
        const std::string& stateName = elem.first;
        auto it = namesToStates.find(stateName);
        assert(it != namesToStates.end());
        auto& state = it->second;

        for (const std::string& branchedStateName : elem.second) {
            auto it = namesToStates.find(branchedStateName);
            if (it == namesToStates.end()) {
                std::cerr << branchedStateName << ": Failed to find definition for state in parse graph." << std::endl;
                return std::shared_ptr<State>();
            }

            state->m_branches.push_back(std::make_shared<Branch>(it->second));
        }
    }

    auto it = namesToStates.find("start");
    if (it == namesToStates.end()) {
        std::cerr << "Failed to find state \"start\" in parse graph." << std::endl;
        return std::shared_ptr<State>();
    }

    return it->second;
}

// Represents a single path in a parse graph from the start state to some
// terminate state.
class ParsePath {
public:
    // A single element in a path: state in its branch chosen to
    // proceed to the next path element.
    struct Element {
        Element(const std::shared_ptr<State>& state) : m_state(state) {}

        std::shared_ptr<State> m_state;
        std::shared_ptr<Branch> m_branch;
    };

    typedef std::vector<Element> Elements;
    typedef Elements::const_iterator Iterator;

public:
    // Adds a new state to the end of the path.
    void pushState(const std::shared_ptr<State>& state) {
        m_elements.emplace_back(state);
    }

    // Removes the last state from the path.
    void popState() {
        assert(!m_elements.empty());
        m_elements.pop_back();
    }

    // Checks to see if the branch can be followed to proceed building the path
    // and, if it can, registers the branch in the path.
    // The branch must belong to the last state pushed to the path.
    // A branch cannot be followed if:
    //   - it branches to the branching state itself (the last state loops to itself);
    //   - it has already been followed by this path (in order not to follow the same loop
    //     multiple times and hang there).
    bool followBranch(const std::shared_ptr<Branch>& branch) {
        assert(!m_elements.empty());

        // The last registered state is the branching state (the one owning this branch).
        const std::shared_ptr<State> lastState = m_elements.back().m_state;

        if (branch->m_nextState == lastState)
            // State branches to itself. Ignore the branch.
            return false;

        // Do not follow the branch if it is already a part of this path.
        for (auto& elem : m_elements) {
            if (elem.m_state == lastState && elem.m_branch == branch)
                return false;
        }

        // The branch can be followed.
        m_elements.back().m_branch = branch;
        return true;
    }

    Iterator begin() const { return m_elements.begin(); }
    Iterator end() const { return m_elements.end(); }

private:
    Elements m_elements;
};

// Outputs the parse path to a given stream.
std::ostream& operator<<(std::ostream& stream, const ParsePath& path) {
    for (auto& elem : path)
        stream << elem.m_state->m_name << "; ";
    return stream;
}

// Paths finding.
void findPathsImpl(std::vector<ParsePath>& paths, ParsePath& currentPath, const std::shared_ptr<State>& state) {
    currentPath.pushState(state);

    if (state->m_branches.empty()) {
        // Terminate state found. Finalize the current path.
        paths.push_back(currentPath);
    } else {
        // There are branches in this state to check.
        // Detect if at least one branch of "state" will be used for looking deeper into the parse graph.
        // If no state's branch is followed means that all the state's branches are already a part
        // of the path being built now. This in turn means either currentPath is a loop and "state" is its
        // entry state that was reached again while traversing the loop and that state has no other
        // branch not yet added to currentPath.
        bool branchFollowed = false;

        for (auto& branch : state->m_branches) {
            if (currentPath.followBranch(branch)) {
                branchFollowed = true;
                findPathsImpl(paths, currentPath, branch->m_nextState);
            }
        }

        if (!branchFollowed) {
            // No branch was followed.
            // This is a loop without exit. Report the looped path.
            std::cerr << "Loop without exit or loop whose all states and branches have already been added to the path detected. "
                      << std::endl << "Ignoring the parse subtree: "
                      << std::endl << "\t" << currentPath << std::endl;
        }

        // Possible to detect loops and collect the states that compose it here if needed.
        // Loop can (and should) be defined as a property of ParsePath.
    }

    currentPath.popState();
}

void findPaths(std::vector<ParsePath>& paths, const std::shared_ptr<State>& state) {
    ParsePath currentPath;
    findPathsImpl(paths, currentPath, state);
}

int main() {
    // Build a graph.
    auto startState = createGraph(ParseGraph);
    if (!startState)
        return 1;

    // Find all paths in the graph.
    std::vector<ParsePath> paths;
    findPaths(paths, startState);

    // Output the paths found.
    std::cout << "Paths found:" << std::endl;
    for (auto& path : paths) {
        std::cout << "\t";
        std::cout << path << std::endl;
    }

    return 0;
}

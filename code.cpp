#include <bits/stdc++.h>
using namespace std;

enum class JudgeStatus {
    Accepted,
    Wrong_Answer,
    Runtime_Error,
    Time_Limit_Exceed
};

static inline bool isAccepted(JudgeStatus s) { return s == JudgeStatus::Accepted; }

static JudgeStatus parseStatus(const string &s) {
    if (s == "Accepted") return JudgeStatus::Accepted;
    if (s == "Wrong_Answer") return JudgeStatus::Wrong_Answer;
    if (s == "Runtime_Error") return JudgeStatus::Runtime_Error;
    // guaranteed valid
    return JudgeStatus::Time_Limit_Exceed;
}

struct SubmissionRecord {
    string teamName;
    int problemIndex; // 0-based
    JudgeStatus status;
    int time;
};

struct ProblemState {
    bool solved = false;
    int solveTime = 0; // time of first AC when revealed/visible
    int wrongAttemptsTotal = 0; // total wrong attempts counted into visible state
    int wrongBeforeSolve = 0; // snapshot when solved

    // Freeze-cycle snapshots
    bool wasSolvedAtFreeze = false; // solved before entering current freeze
    int wrongAttemptsBeforeFreeze = 0; // x used in -x/y display
    vector<pair<JudgeStatus,int>> frozenSubmissions; // submissions after freeze for this problem
};

struct TeamState {
    string name;
    vector<ProblemState> problems;

    // Visible aggregates (respecting freeze)
    int solvedVisible = 0;
    long long penaltyVisible = 0; // sum P = 20*wrongBeforeSolve + solveTime
    vector<int> solveTimesVisible; // times of solved problems (visible ones)

    // For queries
    vector<SubmissionRecord> allSubmissions; // chronological
};

struct SystemState {
    bool started = false;
    bool frozen = false;
    int duration = 0;
    int problemCount = 0;

    // Teams by name
    unordered_map<string, int> teamIndexByName;
    vector<TeamState> teams;

    // Last flushed ranking (vector of indices into teams)
    vector<int> lastFlushedOrder;
    bool hasFlushedAtLeastOnce = false;
    vector<int> teamRank; // 1-based rank positions for last flushed (or lex before first flush)
};

static void resetVisibleAggregates(TeamState &team) {
    team.solvedVisible = 0;
    team.penaltyVisible = 0;
    team.solveTimesVisible.clear();
    for (const auto &ps : team.problems) {
        if (ps.solved) {
            team.solvedVisible += 1;
            team.penaltyVisible += 20LL * ps.wrongBeforeSolve + ps.solveTime;
            team.solveTimesVisible.push_back(ps.solveTime);
        }
    }
    sort(team.solveTimesVisible.begin(), team.solveTimesVisible.end(), greater<int>());
}

static void ensureTeamAggregatesUpToDate(SystemState &sys) {
    for (auto &team : sys.teams) {
        resetVisibleAggregates(team);
    }
}

static bool rankingLess(const SystemState &sys, int lhsIdx, int rhsIdx) {
    const TeamState &A = sys.teams[lhsIdx];
    const TeamState &B = sys.teams[rhsIdx];
    if (A.solvedVisible != B.solvedVisible) return A.solvedVisible > B.solvedVisible;
    if (A.penaltyVisible != B.penaltyVisible) return A.penaltyVisible < B.penaltyVisible;
    // compare solve times: smaller maximum is better; compare descending vectors element-wise
    const vector<int> &ta = A.solveTimesVisible;
    const vector<int> &tb = B.solveTimesVisible;
    size_t n = ta.size(); // equal when solvedVisible equal
    for (size_t i = 0; i < n; ++i) {
        if (ta[i] != tb[i]) return ta[i] < tb[i];
    }
    return A.name < B.name;
}

static vector<int> computeCurrentOrder(SystemState &sys) {
    ensureTeamAggregatesUpToDate(sys);
    vector<int> order(sys.teams.size());
    iota(order.begin(), order.end(), 0);
    stable_sort(order.begin(), order.end(), [&](int a, int b){ return rankingLess(sys, a, b); });
    return order;
}

static void updateLastFlushedOrderLex(SystemState &sys) {
    vector<int> order(sys.teams.size());
    iota(order.begin(), order.end(), 0);
    stable_sort(order.begin(), order.end(), [&](int a, int b){ return sys.teams[a].name < sys.teams[b].name; });
    sys.lastFlushedOrder = move(order);
    sys.hasFlushedAtLeastOnce = false;
    sys.teamRank.assign(sys.teams.size(), 0);
    for (size_t i = 0; i < sys.lastFlushedOrder.size(); ++i) sys.teamRank[sys.lastFlushedOrder[i]] = (int)i + 1;
}

static void performFlush(SystemState &sys) {
    sys.lastFlushedOrder = computeCurrentOrder(sys);
    sys.hasFlushedAtLeastOnce = true;
    sys.teamRank.assign(sys.teams.size(), 0);
    for (size_t i = 0; i < sys.lastFlushedOrder.size(); ++i) sys.teamRank[sys.lastFlushedOrder[i]] = (int)i + 1;
    cout << "[Info]Flush scoreboard.\n";
}

static void printScoreboardLine(const SystemState &sys, int teamIdx, int ranking, ostream &out) {
    const TeamState &team = sys.teams[teamIdx];
    out << team.name << ' ' << ranking << ' ' << team.solvedVisible << ' ' << team.penaltyVisible;
    for (int p = 0; p < sys.problemCount; ++p) {
        const ProblemState &ps = team.problems[p];
        out << ' ';
        if (sys.frozen) {
            if (ps.solved) {
                if (ps.wrongBeforeSolve == 0) out << '+';
                else out << '+' << ps.wrongBeforeSolve;
            } else {
                if (!ps.wasSolvedAtFreeze && !ps.frozenSubmissions.empty()) {
                    int x = ps.wrongAttemptsBeforeFreeze;
                    int y = (int)ps.frozenSubmissions.size();
                    if (x == 0) out << '0' << '/' << y;
                    else out << '-' << x << '/' << y;
                } else {
                    int x = ps.wrongAttemptsTotal;
                    if (x == 0) out << '.';
                    else out << '-' << x;
                }
            }
        } else {
            if (ps.solved) {
                if (ps.wrongBeforeSolve == 0) out << '+';
                else out << '+' << ps.wrongBeforeSolve;
            } else {
                int x = ps.wrongAttemptsTotal;
                if (x == 0) out << '.';
                else out << '-' << x;
            }
        }
    }
    out << "\n";
}

static void printScoreboard(const SystemState &sys, const vector<int> &order) {
    for (size_t i = 0; i < order.size(); ++i) {
        printScoreboardLine(sys, order[i], (int)i + 1, cout);
    }
}

static void enterFreeze(SystemState &sys) {
    if (sys.frozen) {
        cout << "[Error]Freeze failed: scoreboard has been frozen.\n";
        return;
    }
    sys.frozen = true;
    for (auto &team : sys.teams) {
        for (auto &ps : team.problems) {
            ps.wasSolvedAtFreeze = ps.solved;
            ps.wrongAttemptsBeforeFreeze = ps.wrongAttemptsTotal;
            ps.frozenSubmissions.clear();
        }
    }
    cout << "[Info]Freeze scoreboard.\n";
}

static bool teamHasFrozenProblems(const TeamState &team) {
    for (const auto &ps : team.problems) {
        if (!ps.wasSolvedAtFreeze && !ps.frozenSubmissions.empty()) return true;
    }
    return false;
}

static int findLowestRankedTeamWithFrozen(const SystemState &sys, const vector<int> &order) {
    for (int i = (int)order.size() - 1; i >= 0; --i) {
        if (teamHasFrozenProblems(sys.teams[order[i]])) return i;
    }
    return -1;
}

static int firstFrozenProblemIndex(const TeamState &team) {
    for (int p = 0; p < (int)team.problems.size(); ++p) {
        const auto &ps = team.problems[p];
        if (!ps.wasSolvedAtFreeze && !ps.frozenSubmissions.empty()) return p;
    }
    return -1;
}

static void applyUnfreezeOneProblem(SystemState &sys, int teamIdx, int problemIdx) {
    TeamState &team = sys.teams[teamIdx];
    ProblemState &ps = team.problems[problemIdx];
    if (ps.wasSolvedAtFreeze) {
        ps.frozenSubmissions.clear();
        return;
    }
    int wrongAfterFreezeBeforeAC = 0;
    bool solvedNow = false;
    int solveTime = 0;
    for (const auto &entry : ps.frozenSubmissions) {
        if (entry.first == JudgeStatus::Accepted) {
            solvedNow = true;
            solveTime = entry.second;
            break;
        } else {
            wrongAfterFreezeBeforeAC++;
        }
    }
    if (solvedNow) {
        ps.solved = true;
        ps.solveTime = solveTime;
        ps.wrongBeforeSolve = ps.wrongAttemptsBeforeFreeze + wrongAfterFreezeBeforeAC;
        ps.wrongAttemptsTotal = ps.wrongBeforeSolve; // align totals with before-first-AC
    } else {
        // No AC; all after-freeze submissions are visible wrong attempts now
        ps.wrongAttemptsTotal = ps.wrongAttemptsBeforeFreeze + (int)ps.frozenSubmissions.size();
    }
    ps.frozenSubmissions.clear();
    // recompute team aggregates for this team only
    resetVisibleAggregates(team);
}

static void scroll(SystemState &sys) {
    if (!sys.frozen) {
        cout << "[Error]Scroll failed: scoreboard has not been frozen.\n";
        return;
    }
    cout << "[Info]Scroll scoreboard.\n";
    // Flush before scrolling
    sys.lastFlushedOrder = computeCurrentOrder(sys);
    sys.hasFlushedAtLeastOnce = true;
    // Output scoreboard before scrolling (after flushing)
    printScoreboard(sys, sys.lastFlushedOrder);

    // Current order that will update step by step
    vector<int> order = sys.lastFlushedOrder;

    while (true) {
        int idx = findLowestRankedTeamWithFrozen(sys, order);
        if (idx == -1) break;
        int teamIdx = order[idx];
        int probIdx = firstFrozenProblemIndex(sys.teams[teamIdx]);
        if (probIdx == -1) { // should not happen
            // remove this team from consideration and continue
            // advance by moving idx upward
            // But simpler: mark no frozen by clearing and continue
            // (Handled by findLowestRankedTeamWithFrozen next iteration)
        }
        // Apply unfreeze
        applyUnfreezeOneProblem(sys, teamIdx, probIdx);

        // Reposition this team using binary search on the sorted order (excluding the team)
        // Remove the team temporarily
        order.erase(order.begin() + idx);
        int lo = 0, hi = (int)order.size();
        while (lo < hi) {
            int mid = (lo + hi) >> 1;
            if (rankingLess(sys, teamIdx, order[mid])) hi = mid; else lo = mid + 1;
        }
        int newPos = lo;
        // If moved up (better ranking), print ranking-change line
        if (newPos < idx) {
            int replacedTeamIdx = order[newPos];
            const TeamState &t = sys.teams[teamIdx];
            cout << t.name << ' ' << sys.teams[replacedTeamIdx].name << ' ' << t.solvedVisible << ' ' << t.penaltyVisible << "\n";
        }
        order.insert(order.begin() + newPos, teamIdx);
    }

    // Output scoreboard after scrolling
    printScoreboard(sys, order);

    // End of scrolling; lift freeze
    sys.frozen = false;
    for (auto &team : sys.teams) {
        for (auto &ps : team.problems) {
            ps.wasSolvedAtFreeze = false;
            ps.wrongAttemptsBeforeFreeze = ps.wrongAttemptsTotal;
            ps.frozenSubmissions.clear();
        }
    }
    // Treat the final scoreboard after scrolling as flushed for future queries
    sys.lastFlushedOrder = order;
    sys.hasFlushedAtLeastOnce = true;
    sys.teamRank.assign(sys.teams.size(), 0);
    for (size_t i = 0; i < sys.lastFlushedOrder.size(); ++i) sys.teamRank[sys.lastFlushedOrder[i]] = (int)i + 1;
}

static void addTeam(SystemState &sys, const string &teamName) {
    if (sys.started) {
        cout << "[Error]Add failed: competition has started.\n";
        return;
    }
    if (sys.teamIndexByName.find(teamName) != sys.teamIndexByName.end()) {
        cout << "[Error]Add failed: duplicated team name.\n";
        return;
    }
    int idx = (int)sys.teams.size();
    sys.teamIndexByName[teamName] = idx;
    TeamState team;
    team.name = teamName;
    sys.teams.push_back(move(team));
    cout << "[Info]Add successfully.\n";
}

static void startCompetition(SystemState &sys, int duration, int problemCount) {
    if (sys.started) {
        cout << "[Error]Start failed: competition has started.\n";
        return;
    }
    sys.started = true;
    sys.duration = duration;
    sys.problemCount = problemCount;
    for (auto &team : sys.teams) {
        team.problems.assign(problemCount, ProblemState{});
        resetVisibleAggregates(team);
    }
    // Initialize last flushed order by lex order per spec
    updateLastFlushedOrderLex(sys);
    cout << "[Info]Competition starts.\n";
}

static void submit(SystemState &sys, char problemChar, const string &teamName, JudgeStatus status, int time) {
    auto it = sys.teamIndexByName.find(teamName);
    if (it == sys.teamIndexByName.end()) return; // input guaranteed valid
    int tIdx = it->second;
    int pIdx = problemChar - 'A';
    TeamState &team = sys.teams[tIdx];
    // Record for query
    team.allSubmissions.push_back(SubmissionRecord{teamName, pIdx, status, time});

    ProblemState &ps = team.problems[pIdx];

    if (!sys.frozen) {
        if (ps.solved) {
            return;
        }
        if (isAccepted(status)) {
            ps.solved = true;
            ps.solveTime = time;
            ps.wrongBeforeSolve = ps.wrongAttemptsTotal;
        } else {
            ps.wrongAttemptsTotal += 1;
        }
        resetVisibleAggregates(team);
    } else {
        // frozen
        if (ps.wasSolvedAtFreeze) {
            // Submissions to problems solved before freeze are ignored for scoreboard
            return;
        }
        ps.frozenSubmissions.emplace_back(status, time);
        // aggregates unchanged until scroll
    }
}

static void queryRanking(const SystemState &sys, const string &teamName) {
    auto it = sys.teamIndexByName.find(teamName);
    if (it == sys.teamIndexByName.end()) {
        cout << "[Error]Query ranking failed: cannot find the team.\n";
        return;
    }
    cout << "[Info]Complete query ranking.\n";
    if (sys.frozen) {
        cout << "[Warning]Scoreboard is frozen. The ranking may be inaccurate until it were scrolled.\n";
    }
    int teamIdx = it->second;
    int ranking = sys.teamRank.empty() ? 0 : sys.teamRank[teamIdx];
    cout << teamName << " NOW AT RANKING " << ranking << "\n";
}

static void querySubmission(const SystemState &sys, const string &teamName, const string &problemFilter, const string &statusFilter) {
    auto it = sys.teamIndexByName.find(teamName);
    if (it == sys.teamIndexByName.end()) {
        cout << "[Error]Query submission failed: cannot find the team.\n";
        return;
    }
    const TeamState &team = sys.teams[it->second];
    cout << "[Info]Complete query submission.\n";

    int problemIdxFilter = -1;
    if (problemFilter != "ALL") problemIdxFilter = problemFilter[0] - 'A';

    bool statusAll = (statusFilter == "ALL");
    JudgeStatus statusParsed = JudgeStatus::Accepted;
    if (!statusAll) statusParsed = parseStatus(statusFilter);

    for (int i = (int)team.allSubmissions.size() - 1; i >= 0; --i) {
        const auto &rec = team.allSubmissions[i];
        if (problemIdxFilter != -1 && rec.problemIndex != problemIdxFilter) continue;
        if (!statusAll && rec.status != statusParsed) continue;
        // print
        char probChar = char('A' + rec.problemIndex);
        string s;
        switch (rec.status) {
            case JudgeStatus::Accepted: s = "Accepted"; break;
            case JudgeStatus::Wrong_Answer: s = "Wrong_Answer"; break;
            case JudgeStatus::Runtime_Error: s = "Runtime_Error"; break;
            case JudgeStatus::Time_Limit_Exceed: s = "Time_Limit_Exceed"; break;
        }
        cout << teamName << ' ' << probChar << ' ' << s << ' ' << rec.time << "\n";
        return;
    }
    cout << "Cannot find any submission.\n";
}

static inline vector<string_view> splitBySpace(const string &line) {
    vector<string_view> tokens;
    size_t n = line.size();
    size_t i = 0;
    while (i < n) {
        while (i < n && line[i] == ' ') ++i;
        if (i >= n) break;
        size_t j = i;
        while (j < n && line[j] != ' ') ++j;
        tokens.emplace_back(&line[i], j - i);
        i = j;
    }
    return tokens;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    SystemState sys;

    string line;
    while (std::getline(cin, line)) {
        if (line.empty()) continue;
        auto tokens = splitBySpace(line);
        if (tokens.empty()) continue;
        if (tokens[0] == "ADDTEAM") {
            string teamName(tokens[1]);
            addTeam(sys, teamName);
        } else if (tokens[0] == "START") {
            // Format: START DURATION [duration_time] PROBLEM [problem_count]
            int duration = stoi(string(tokens[2]));
            int probCnt = stoi(string(tokens[4]));
            startCompetition(sys, duration, probCnt);
        } else if (tokens[0] == "SUBMIT") {
            // SUBMIT [problem_name] BY [team_name] WITH [submit_status] AT [time]
            char problemChar = string(tokens[1])[0];
            string teamName(tokens[3]);
            string statusStr(tokens[5]);
            int t = stoi(string(tokens[7]));
            submit(sys, problemChar, teamName, parseStatus(statusStr), t);
        } else if (tokens[0] == "FLUSH") {
            performFlush(sys);
        } else if (tokens[0] == "FREEZE") {
            enterFreeze(sys);
        } else if (tokens[0] == "SCROLL") {
            scroll(sys);
        } else if (tokens[0] == "QUERY_RANKING") {
            string teamName(tokens[1]);
            queryRanking(sys, teamName);
        } else if (tokens[0] == "QUERY_SUBMISSION") {
            // QUERY_SUBMISSION [team_name] WHERE PROBLEM=[problem_name] AND STATUS=[status]
            string teamName(tokens[1]);
            string problemToken(tokens[3]);
            string statusToken(tokens[5]);
            string problemFilter = problemToken.substr(problemToken.find('=') + 1);
            string statusFilter = statusToken.substr(statusToken.find('=') + 1);
            querySubmission(sys, teamName, problemFilter, statusFilter);
        } else if (tokens[0] == "END") {
            cout << "[Info]Competition ends.\n";
            break;
        } else {
            // ignore invalid (not expected)
        }
    }

    return 0;
}

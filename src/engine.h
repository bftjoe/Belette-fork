#ifndef ENGINE_H_INCLUDED
#define ENGINE_H_INCLUDED

#include "chess.h"
#include "position.h"
#include "evaluate.h"
#include "move.h"
#include "tt.h"
#include "utils.h"

namespace BabChess {

struct SearchLimits {
    TimeMs timeLeft[NB_SIDE] = {0};
    TimeMs increment[NB_SIDE] = {0};
    int movesToGo = 0;
    int maxDepth = 0;
    size_t maxNodes = 0;
    TimeMs maxTime = 0;
    MoveList searchMoves;
};

struct SearchData {
    SearchData(const Position& pos_, const SearchLimits& limits_)
    : position(pos_), limits(limits_), nbNodes(0), killerMoves{MOVE_NONE}, counterMoves{MOVE_NONE} { 
        start();
    }

    void initAllocatedTime();

    inline TimeMs getElapsed() { return now() - startTime; }
    inline void start() {
        startTime = now();
        initAllocatedTime();
    }
    
    inline bool useTournamentTime() { return !!(limits.timeLeft[WHITE] | limits.timeLeft[WHITE]); }
    inline bool useFixedTime() { return limits.maxTime > 0; }
    inline bool useTimeLimit() { return useTournamentTime() || useTimeLimit(); }
    inline bool useNodeCountLimit() { return limits.maxNodes > 0; }

    inline bool shouldStop() {
        // Check time every 1024 nodes for performance reason
        if (nbNodes % 1024 != 0)  return false;
        
        TimeMs elapsed = now() - startTime;

        if (useTournamentTime() && elapsed >= allocatedTime)
            return true;
        if (useFixedTime() && (elapsed > limits.maxTime))
            return true;
        if (useNodeCountLimit() && nbNodes >= limits.maxNodes)
            return true;
        
        return false;
    }

    inline void clearKillers(int ply) {
        killerMoves[ply][0] = killerMoves[ply][1] = MOVE_NONE;
    }

    inline void updateKillers(Move move, int ply) {
        if (killerMoves[ply][0] != move) {
            killerMoves[ply][1] = killerMoves[ply][0];
            killerMoves[ply][0] = move;
        }
    }

    inline void updateCounter(Move move) {
        Move prevMove = position.previousMove();
        if (prevMove != MOVE_NONE)
            counterMoves[position.getPieceAt(moveTo(prevMove))][moveTo(prevMove)] = move;
    }

    inline Move getCounter() const {
        Move prevMove = position.previousMove();
        if (prevMove == MOVE_NONE) return MOVE_NONE;

        return counterMoves[position.getPieceAt(moveTo(prevMove))][moveTo(prevMove)];
    }

    Position position;
    SearchLimits limits;
    size_t nbNodes;

    TimeMs startTime;
    TimeMs lastCheck;
    TimeMs allocatedTime;

    Move killerMoves[MAX_PLY][2];
    Move counterMoves[NB_PIECE][NB_SQUARE];
};

struct SearchEvent {
    SearchEvent(int depth_, const MoveList &pv_, Score bestScore_, size_t nbNode_, TimeMs elapsed_, size_t hashfull_): 
        depth(depth_), pv(pv_), bestScore(bestScore_), nbNodes(nbNode_), elapsed(elapsed_), hashfull(hashfull_) { }

    int depth;
    const MoveList &pv;
    Score bestScore;
    size_t nbNodes;
    TimeMs elapsed;
    size_t hashfull;
};

enum class NodeType {
    Root,
    PV,
    NonPV
};

class Engine {
public:
    Engine() = default;
    virtual ~Engine() = default;

    inline Position &position() { return rootPosition; }
    inline const Position &position() const { return rootPosition; }

    void search(const SearchLimits &limits);
    void stop();
    inline bool isSearching() { return searching; }
    inline bool searchAborted() { return aborted; }
    inline TranspositionTable& getTT() { return tt; }

    virtual void onSearchProgress(const SearchEvent &event) = 0;
    virtual void onSearchFinish(const SearchEvent &event) = 0;

private:
    Position rootPosition;
    TranspositionTable tt;
    bool aborted = true;
    bool searching = false;

    inline void idSearch(SearchData &sd) { rootPosition.getSideToMove() == WHITE ? idSearch<WHITE>(sd) : idSearch<BLACK>(sd); }
    template<Side Me> void idSearch(SearchData &sd);

    template<Side Me, NodeType NT> Score pvSearch(SearchData &sd, Score alpha, Score beta, int depth, int ply, MoveList &pv);

    template<Side Me, NodeType NT> Score qSearch(SearchData &sd, Score alpha, Score beta, int depth, int ply, MoveList &pv);
};

} /* namespace BabChess */

#endif /* ENGINE_H_INCLUDED */

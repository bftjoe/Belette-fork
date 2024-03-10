#ifndef MOVEPICKER_H_INCLUDED
#define MOVEPICKER_H_INCLUDED

#include <cstdint>
#include <algorithm>
#include "fixed_vector.h"
#include "chess.h"
#include "position.h"
#include "move.h"
#include "evaluate.h"

namespace BabChess {

struct ScoredMove {
    void (Position::*doMove)(Move m);
    void (Position::*undoMove)(Move m);
    Move move;
    int16_t score;
};

using ScoredMoveList = fixed_vector<ScoredMove, MAX_MOVE, uint8_t>;

enum MovePickerType {
    MAIN,
    QUIESCENCE
};

template<MovePickerType Type, Side Me>
class MovePicker {
public:
    MovePicker(const Position &pos_, Move ttMove_ = MOVE_NONE, Move killer1_ = MOVE_NONE, Move killer2_ = MOVE_NONE, Move counter_ = MOVE_NONE)
    : pos(pos_), ttMove(ttMove_), refutations{killer1_, killer2_, counter_} 
    { 
        assert(killer1_ != killer2_ || killer1_ == MOVE_NONE);
    }

    template<typename Handler>
    inline bool enumerate(const Handler &handler);

private:
    const Position &pos;
    Move ttMove;
    Move refutations[3];

    Bitboard threatenedPieces;

    inline int16_t scoreEvasion(Move m);
    inline int16_t scoreTactical(Move m);
    inline int16_t scoreQuiet(Move m);
};


template<MovePickerType Type, Side Me>
template<typename Handler>
bool MovePicker<Type, Me>::enumerate(const Handler &handler) {
    // TTMove
    if (pos.isLegal<Me>(ttMove)) {
        if (!handler(ttMove, &Position::doMove<Me>, &Position::undoMove<Me>)) return false;
    }
    
    ScoredMoveList moves;
    ScoredMove *current, *endBadTacticals, *beginQuiets, *endBadQuiets;

    // Evasions
    if (pos.inCheck()) {
        enumerateLegalMoves<Me, ALL_MOVES>(pos, [&](Move m, auto doMove, auto undoMove) {
            if (m == ttMove) return true; // continue;

            ScoredMove smove = {doMove, undoMove, m, scoreEvasion(m)};
            moves.push_back(smove);
            return true;
        });

        std::sort(moves.begin(), moves.end(), [](const ScoredMove &a, const ScoredMove &b) {
            return a.score > b.score;
        });

        for (auto m : moves) {
            if (m.move == ttMove) continue;
            if (!handler(m.move, m.doMove, m.undoMove)) return false;
        }

        return true;
    }

    // Tacticals
    enumerateLegalMoves<Me, TACTICAL_MOVES>(pos, [&](Move m, auto doMove, auto undoMove) {
        if (m == ttMove) return true; // continue;

        if constexpr(Type == QUIESCENCE) {
            if (!pos.see(m, 0)) return true; // continue;
        }
        
        ScoredMove smove = {doMove, undoMove, m, scoreTactical(m)};
        moves.push_back(smove);
        return true;
    });

    std::sort(moves.begin(), moves.end(), [](const ScoredMove &a, const ScoredMove &b) {
        return a.score > b.score;
    });

    // Good tacticals
    for (current = endBadTacticals = moves.begin(); current != moves.end(); current++) {
        if constexpr(Type == MAIN) { // For quiescence bad moves are already pruned in move enumeration
            if (!pos.see(current->move, -50)) { // Allow Bishop takes Knight
                *endBadTacticals++ = *current;
                continue;
            }
        }

        if (!handler(current->move, current->doMove, current->undoMove)) return false;
    }

    // Stop here for Quiescence
    if constexpr(Type == QUIESCENCE) return true;

    // Killer 1
    if (refutations[0] != ttMove && !pos.isTactical(refutations[0]) && pos.isLegal<Me>(refutations[0])) {
        if (!handler(refutations[0], &Position::doMove<Me>, &Position::undoMove<Me>)) return false;
    }

    // Killer 2
    if (refutations[1] != ttMove && !pos.isTactical(refutations[1]) && pos.isLegal<Me>(refutations[1])) {
        if (!handler(refutations[1], &Position::doMove<Me>, &Position::undoMove<Me>)) return false;
    }

    // Counter
    if (refutations[2] != ttMove && !pos.isTactical(refutations[2]) && refutations[2] != refutations[0] && refutations[2] != refutations[1] && pos.isLegal<Me>(refutations[2])) {
        if (!handler(refutations[2], &Position::doMove<Me>, &Position::undoMove<Me>)) return false;
    }

    // Quiets
    moves.resize(endBadTacticals - moves.begin()); // Keep only bad tacticals
    threatenedPieces = (pos.getPiecesBB(Me, KNIGHT, BISHOP) & pos.threatenedByPawns())
                     | (pos.getPiecesBB(Me, ROOK) & pos.threatenedByMinors())
                     | (pos.getPiecesBB(Me, QUEEN) & pos.threatenedByRooks());

    enumerateLegalMoves<Me, QUIET_MOVES>(pos, [&](Move move, auto doMove, auto undoMove) {
        if (move == ttMove) return true; // continue;
        if (refutations[0] == move || refutations[1] == move || refutations[2] == move) return true; // continue

        ScoredMove smove = {doMove, undoMove, move, scoreQuiet(move)};
        moves.push_back(smove);
        return true;
    });

    std::sort(moves.begin(), moves.end(), [](const ScoredMove &a, const ScoredMove &b) {
        return a.score > b.score;
    });

    // Good quiets
    for (current = beginQuiets = endBadQuiets = endBadTacticals; current != moves.end(); current++) {
        if (current->score < 0) {
            *endBadQuiets++ = *current;
            continue;
        }

        if (!handler(current->move, current->doMove, current->undoMove)) return false;
    }

    // Bad tacticals
    for (current = moves.begin(); current != endBadTacticals; current++) {
        if (!handler(current->move, current->doMove, current->undoMove)) return false;
    }

    // Bad quiets
    for (current = beginQuiets; current != endBadQuiets; current++) {
        if (!handler(current->move, current->doMove, current->undoMove)) return false;
    }

    return true;
}

template<MovePickerType Type, Side Me>
int16_t MovePicker<Type, Me>::scoreEvasion(Move m) {
    if (pos.isCapture(m)) {
        return scoreTactical(m);
    }

    return 0;
}

template<MovePickerType Type, Side Me>
int16_t MovePicker<Type, Me>::scoreTactical(Move m) {
    return PieceValue<MG>(pos.getPieceAt(moveTo(m))) - (int)pieceType(pos.getPieceAt(moveFrom(m))); // MVV-LVA
}

template<MovePickerType Type, Side Me>
int16_t MovePicker<Type, Me>::scoreQuiet(Move m) {
    Square from = moveFrom(m), to = moveTo(m);
    PieceType pt = pieceType(pos.getPieceAt(from));
    Score score = NB_PIECE_TYPE-(int)pt;

    if (moveType(m) == PROMOTION) [[unlikely]]
        return -100;

    if (threatenedPieces & from) {
        score += pt == QUEEN && !(to & pos.threatenedByRooks()) ? 1000
             : pt == ROOK && !(to & pos.threatenedByMinors()) ? 500
             : (pt == BISHOP || pt == KNIGHT) && !(to & pos.threatenedByPawns()) ? 300
             : 0;
    }

    switch (pt) {
        case PAWN:
            if (pawnAttacks<Me>(bb(to)) & pos.getPiecesBB(~Me, KING)) {
                score += 10;
            }
            break;
        case KNIGHT:
            if (attacks<KNIGHT>(to, pos.getPiecesBB()) & pos.getPiecesBB(~Me, KING)) {
                score += 10;
            }
            break;
        case BISHOP:
            if (attacks<BISHOP>(to, pos.getPiecesBB()) & pos.getPiecesBB(~Me, KING)) {
                score += 10;
            }
        case ROOK:
            if (attacks<ROOK>(to, pos.getPiecesBB()) & pos.getPiecesBB(~Me, KING)) {
                score += 10;
            }
        case QUEEN:
            if (attacks<QUEEN>(to, pos.getPiecesBB()) & pos.getPiecesBB(~Me, KING)) {
                score += 10;
            }
        default:
            break;
    }

    return score;
}

} /* namespace BabChess */

#endif /* MOVEPICKER_H_INCLUDED */

/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>

#include "bitboard.h"
#include "endgame.h"
#include "movegen.h"
#include "uci.h"

using std::string;

namespace {

  // Table used to drive the king towards the edge of the board
  // in KX vs K and KQ vs KR endgames.
  const int PushToEdges[SQUARE_NB] = {
    100, 90, 80, 70, 70, 80, 90, 100,
     90, 70, 60, 50, 50, 60, 70,  90,
     80, 60, 40, 30, 30, 40, 60,  80,
     70, 50, 30, 20, 20, 30, 50,  70,
     70, 50, 30, 20, 20, 30, 50,  70,
     80, 60, 40, 30, 30, 40, 60,  80,
     90, 70, 60, 50, 50, 60, 70,  90,
    100, 90, 80, 70, 70, 80, 90, 100
  };

  // Table used to drive the king towards a corner square of the
  // right color in KBN vs K endgames.
  const int PushToCorners[SQUARE_NB] = {
    200, 190, 180, 170, 160, 150, 140, 130,
    190, 180, 170, 160, 150, 140, 130, 140,
    180, 170, 155, 140, 140, 125, 140, 150,
    170, 160, 140, 120, 110, 140, 150, 160,
    160, 150, 140, 110, 120, 140, 160, 170,
    150, 140, 125, 140, 140, 155, 170, 180,
    140, 130, 140, 150, 160, 170, 180, 190,
    130, 140, 150, 160, 170, 180, 190, 200
  };

  // Table used to drive the king towards the edge of the board
  // in KBQ vs K.
  const int PushToOpposingSideEdges[SQUARE_NB] = {
     30, 10,  5,  0,  0,  5, 10,  30,
     40, 20,  5,  0,  0,  5, 20,  40,
     50, 30, 10,  0,  0, 10, 30,  50,
     60, 40, 20, 10, 10, 20, 40,  60,
     70, 50, 30, 20, 20, 30, 50,  70,
     80, 60, 40, 30, 30, 40, 60,  80,
     90, 70, 60, 50, 50, 60, 70,  90,
    100, 90, 80, 70, 70, 80, 90, 100
  };

  // Tables used to drive a piece towards or away from another piece
  const int PushClose[8] = { 0, 0, 100, 80, 60, 40, 20, 10 };
  const int PushAway [8] = { 0, 5, 20, 40, 60, 80, 90, 100 };

  // Pawn Rank based scaling factors used in KRPPKRP endgame
  const int KRPPKRPScaleFactors[RANK_NB] = { 0, 9, 10, 14, 21, 44, 0, 0 };

#ifndef NDEBUG
  bool verify_material(const Position& pos, Color c, Value npm, int pawnsCnt) {
    return pos.non_pawn_material(c) == npm && pos.count<PAWN>(c) == pawnsCnt;
  }
#endif

  // Map the square as if strongSide is white and strongSide's only pawn
  // is on the left half of the board.
  Square normalize(const Position& pos, Color strongSide, Square sq) {

    assert(pos.count<PAWN>(strongSide) == 1);

    if (file_of(pos.square<PAWN>(strongSide)) >= FILE_E)
        sq = Square(sq ^ 7); // Mirror SQ_H1 -> SQ_A1

    if (strongSide == BLACK)
        sq = ~sq;

    return sq;
  }

} // namespace


/// Endgames members definitions

Endgames::Endgames() {

  add<KPK>("KPK");
  add<KNNK>("KNNK");
  add<KBNK>("KSNK");
  add<KBQK>("KSMK");
  add<KNQK>("KNMK");
  add<KRKP>("KRKP");
  add<KRKB>("KRKS");
  add<KRKN>("KRKN");
  add<KRKQ>("KRKM");

  add<KNPK>("KNPK");
  add<KNPKB>("KNPKS");
  add<KRPKR>("KRPKR");
  add<KBPKB>("KSPKS");
  add<KBPKN>("KSPKN");
  add<KBPPKB>("KSPPKS");
  add<KRPPKRP>("KRPPKRP");
}


/// Mate with KX vs K. This function is used to evaluate positions with
/// king and plenty of material vs a lone king. It simply gives the
/// attacking side a bonus for driving the defending king towards the edge
/// of the board, and for keeping the distance between the two kings small.
template<>
Value Endgame<KXK>::operator()(const Position& pos) const {

  assert(!pos.checkers()); // Eval is never called when in check

  // Stalemate detection with lone king
  if (pos.side_to_move() == weakSide && !MoveList<LEGAL>(pos).size())
      return VALUE_DRAW;

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                - pos.non_pawn_material(weakSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - pos.count<PAWN>(weakSide) * PawnValueEg
                + PushToEdges[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if (pos.count<ALL_PIECES>(weakSide) == 1)
  {
      if (Options["EnableCounting"])
          result = result * std::max(2 * pos.counting_limit() - pos.rule50_count(), 0) / 128;
      else if (   pos.count<  ROOK>(strongSide)
              || pos.count<BISHOP>(strongSide) >= 2
              ||(pos.count<BISHOP>(strongSide) && pos.count<KNIGHT>(strongSide))
              ||(pos.count<BISHOP>(strongSide) && pos.count<QUEEN>(strongSide))
              ||(pos.count<KNIGHT>(strongSide) && pos.count<QUEEN>(strongSide) >= 2)
              ||(pos.count< QUEEN>(strongSide) >= 3
                  && ( DarkSquares & pos.pieces(strongSide, QUEEN))
                  && (~DarkSquares & pos.pieces(strongSide, QUEEN))))
          result = std::min(result + VALUE_KNOWN_WIN, VALUE_MATE_IN_MAX_PLY - 1);
  }

  return strongSide == pos.side_to_move() ? result : -result;
}

/// KQsPs vs K.
template<>
Value Endgame<KQsPsK>::operator()(const Position& pos) const {

  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));
  assert(!pos.checkers()); // Eval is never called when in check

  // Stalemate detection with lone king
  if (pos.side_to_move() == weakSide && !MoveList<LEGAL>(pos).size())
      return VALUE_DRAW;

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToEdges[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if (   pos.count<QUEEN>(strongSide) >= 3
      && ( DarkSquares & pos.pieces(strongSide, QUEEN))
      && (~DarkSquares & pos.pieces(strongSide, QUEEN)))
      result = std::min(result + VALUE_KNOWN_WIN, VALUE_MATE_IN_MAX_PLY - 1);
  else
  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      // Determine the color of queens from promoting pawns
      Bitboard b = pos.pieces(strongSide, PAWN);
      while (b && (!dark || !light))
      {
          if (file_of(pop_lsb(&b)) % 2 == (strongSide == WHITE ? 0 : 1))
              light = true;
          else
              dark = true;
      }
      if (!dark || !light)
          return VALUE_DRAW; // we can not checkmate with same colored queens
  }

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Mate with KBN vs K.
template<>
Value Endgame<KBNK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + BishopValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  VALUE_KNOWN_WIN
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}

template<>
Value Endgame<KNQK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  return VALUE_DRAW;
}

template<>
Value Endgame<KBQK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  VALUE_KNOWN_WIN
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}

/// KP vs K. This endgame is evaluated with the help of a bitbase.
template<>
Value Endgame<KPK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, VALUE_ZERO, 1));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  return VALUE_DRAW;
}


/// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without
/// a bitbase. The function below returns drawish scores when the pawn is
/// far advanced with support of the king, while the attacking king is far
/// away.
template<>
Value Endgame<KRKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 1));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  RookValueEg
                - PawnValueEg
                + PushToEdges[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KB.
template<>
Value Endgame<KRKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  RookValueEg
                - BishopValueEg
                + PushToEdges[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KN.
template<>
Value Endgame<KRKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Square bksq = pos.square<KING>(weakSide);
  Square bnsq = pos.square<KNIGHT>(weakSide);
  Value result = Value(PushToEdges[bksq] + PushAway[distance(bksq, bnsq)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KQ.
template<>
Value Endgame<KRKQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  RookValueEg
                - QueenValueEg
                + PushToEdges[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Some cases of trivial draws
template<> Value Endgame<KNNK>::operator()(const Position&) const { return VALUE_DRAW; }


/// KRP vs KR.
template<>
ScaleFactor Endgame<KRPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 1));
  assert(verify_material(pos, weakSide,   RookValueMg, 0));

  return SCALE_FACTOR_DRAW;
}

/// KRPP vs KRP. There is just a single rule: if the stronger side has no passed
/// pawns and the defending king is actively placed, the position is drawish.
template<>
ScaleFactor Endgame<KRPPKRP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 2));
  assert(verify_material(pos, weakSide,   RookValueMg, 1));

  return SCALE_FACTOR_DRAW;
}


/// K and two or more pawns vs K.
template<>
ScaleFactor Endgame<KPsK>::operator()(const Position& pos) const {

  assert(pos.non_pawn_material(strongSide) == VALUE_ZERO);
  assert(pos.count<PAWN>(strongSide) >= 2);
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  if (pos.count<PAWN>(strongSide) == 2)
      return SCALE_FACTOR_DRAW;

  return SCALE_FACTOR_NONE;
}


/// KBP vs KB.
template<>
ScaleFactor Endgame<KBPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg, 1));
  assert(verify_material(pos, weakSide,   BishopValueMg, 0));

  return SCALE_FACTOR_DRAW;
}


/// KBPP vs KB.
template<>
ScaleFactor Endgame<KBPPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg, 2));
  assert(verify_material(pos, weakSide,   BishopValueMg, 0));

  return SCALE_FACTOR_NONE;
}


/// KBP vs KN.
template<>
ScaleFactor Endgame<KBPKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg, 1));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  return SCALE_FACTOR_DRAW;
}


/// KNP vs K.
template<>
ScaleFactor Endgame<KNPK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg, 1));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  return SCALE_FACTOR_DRAW;
}


/// KNP vs KB. If knight can block bishop from taking pawn, it's a win.
/// Otherwise the position is drawn.
template<>
ScaleFactor Endgame<KNPKB>::operator()(const Position& pos) const {
  return SCALE_FACTOR_DRAW;
}


/// KP vs KP.
template<>
ScaleFactor Endgame<KPKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, VALUE_ZERO, 1));
  assert(verify_material(pos, weakSide,   VALUE_ZERO, 1));

  return SCALE_FACTOR_DRAW;
}

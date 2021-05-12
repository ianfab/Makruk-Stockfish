/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2021 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
  // in KX vs K endgames.
  constexpr int PushToEdges[SQUARE_NB] = {
    100, 90, 80, 70, 70, 80, 90, 100,
     90, 70, 60, 50, 50, 60, 70,  90,
     80, 60, 40, 30, 30, 40, 60,  80,
     70, 50, 30, 20, 20, 30, 50,  70,
     70, 50, 30, 20, 20, 30, 50,  70,
     80, 60, 40, 30, 30, 40, 60,  80,
     90, 70, 60, 50, 50, 60, 70,  90,
    100, 90, 80, 70, 70, 80, 90, 100
  };

  // Corn redghost
  constexpr int PushToCorn[SQUARE_NB] = {
    200, 150, 100, 70, 70, 100, 150, 200,
    150,  70,  60, 50, 50,  60,  70, 150,
    100,  60,  40, 30, 30,  40,  60, 100,
     70,  50,  30, 20, 20,  30,  50,  70,
     70,  50,  30, 20, 20,  30,  50,  70,
    100,  60,  40, 30, 30,  40,  60, 100,
    150,  70,  60, 50, 50,  60,  70, 150,
    200, 150, 100, 70, 70, 100, 150, 200
  };

  // Table used to drive the king towards the edge of the board
  // in KBQ vs K.
  constexpr int PushToOpposingSideEdges[SQUARE_NB] = {
     30,  5,  3,  0,  0,  3,  5,  30,
     40, 20,  5,  0,  0,  5, 20,  40,
     50, 30, 10,  3,  3, 10, 30,  50,
     60, 40, 20,  7,  7, 20, 40,  60,
     70, 50, 30, 20, 20, 30, 50,  70,
     80, 60, 40, 30, 30, 40, 60,  80,
     90, 70, 60, 50, 50, 60, 70,  90,
    100, 90, 80, 70, 70, 80, 90, 100
  };

  // Table used to drive the king towards a corner
  // of the same color as the queen in KNQ vs K endgames.
  constexpr int PushToQueenCorners[SQUARE_NB] = {
    100, 90, 80, 70, 50, 30,  0,   0,
     90, 70, 60, 50, 30, 10,  0,   0,
     80, 60, 40, 30, 10,  0, 10,  30,
     70, 50, 30, 10,  0, 10, 30,  50,
     50, 30, 10,  0, 10, 30, 50,  70,
     30, 10,  0, 10, 30, 40, 60,  80,
      0,  0, 10, 30, 50, 60, 70,  90,
      0,  0, 30, 50, 70, 80, 90, 100
  };

  // Tables used to drive a piece towards or away from another piece
  constexpr int PushClose[8] = { 0, 0, 100, 80, 60, 40, 20, 10 };
  constexpr int PushAway [8] = { 0, 5, 20, 40, 60, 80, 90, 100 };
  constexpr int PushWin[8] = { 0, 120, 100, 80, 60, 40, 20, 10 }; // redghost

#ifndef NDEBUG
  bool verify_material(const Position& pos, Color c, Value npm, int pawnsCnt) {
    return pos.non_pawn_material(c) == npm && pos.count<PAWN>(c) == pawnsCnt;
  }
#endif

} // namespace


namespace Endgames {

  std::pair<Map<Value>, Map<ScaleFactor>> maps;

  void init() {

    add<KNNK>("KNNK");
    add<KNK>("KNK");
    add<KBK>("KSK");
    add<KBKQ>("KSKM");
    add<KBKP>("KSKP");
    add<KQK>("KMK");
    add<KQKP>("KMKP");
    add<KQQK>("KMMK");
    add<KQPK>("KMPK");
    add<KPPK>("KPPK");
    add<KPK>("KPK");
    add<KNKP>("KNKP");
    add<KNKQ>("KNKM");
    add<KNKB>("KNKS");
    add<KBQK>("KSMK");
    add<KNQK>("KNMK");
    add<KRKN>("KRKN");
    add<KRQQKR>("KRMMKR");
    add<KNQQQKR>("KNMMMKR");
    add<KBQQQKR>("KSMMMKR");
    add<KRNKR>("KRNKR");
    add<KRBKR>("KRSKR");
    add<KRQKR>("KRMKR");
    add<KRPKR>("KRPKR");
    add<KNQKQQ>("KNMKMM");
    add<KNPK>("KNPK");
    add<KNPKP>("KNPKP");
    add<KNPKQ>("KNPKM");
    add<KNPKQQ>("KNPKMM");
    add<KNQKP>("KNMKP");
    add<KNQKQ>("KNMKM");
    add<KNPKB>("KNPKS");
    add<KNQKB>("KNMKS");
    add<KNPKN>("KNPKN");
    add<KNQKN>("KNMKN");
    add<KRQPKR>("KRMPKR");
    add<KRPPKR>("KRPPKR");
    add<KNNKP>("KNNKP");
    add<KNNKQ>("KNNKM");
    add<KNNKB>("KNNKS");
    add<KNNKN>("KNNKN");
    add<KNNKR>("KNNKR");
    add<KBQQKN>("KSMMKN");
    add<KNQQKN>("KNMMKN");
    add<KNQQKBQ>("KNMMKSM");
    add<KNBQKR>("KNSMKR");
    add<KNBQQKRQ>("KNSMMKRM");
    add<KRQQKRQ>("KRMMKRM");
    add<KRNQKRB>("KRNMKRS");
    add<KRBQKRQ>("KRSMKRM");
    add<KBQQKBQ>("KSMMKSM");
    add<KBQPKBQ>("KSMPKSM");
    add<KBPPKBQ>("KSPPKSM");
    add<KQQQKQQ>("KMMMKMM");
    add<KQQKQ>("KMMKM");
    add<KBQKB>("KSMKS");
    add<KQQQKB>("KMMMKS");
    add<KBPKB>("KSPKS");
    add<KQQPKB>("KMMPKS");
    add<KQPPKB>("KMPPKS");
    add<KPPPKB>("KPPPKS");
    add<KNQPKN>("KNMPKN");
    add<KNPPKN>("KNPPKN");
    add<KBQPKN>("KSMPKN");
    add<KBPPKN>("KSPPKN");
    add<KRNPKRB>("KRNPKRS");
    add<KRBPKRQ>("KRSPKRM");
    add<KNPPPKR>("KNPPPKR");
    add<KNQPPKR>("KNMPPKR");
    add<KNQQPKR>("KNMMPKR");
    add<KBPPPKR>("KSPPPKR");
    add<KBQPPKR>("KSMPPKR");
    add<KBQQPKR>("KSMMPKR");
    add<KNQQKQQ>("KNMMKMM");
    add<KNQPKQQ>("KNMPKMM");
    add<KNPPKQQ>("KNPPKMM");
    add<KNNPKNP>("KNNPKNP");
    add<KNNPKNQ>("KNNPKNM");
    add<KNNPKNB>("KNNPKNS");
    add<KNQQQKNQ>("KNMMMKNM");
    add<KNNKPP>("KNNKPP");
    add<KNNKQP>("KNNKMP");
    add<KNNKQQ>("KNNKMM");
    add<KNNKBP>("KNNKSP");
    add<KNNKBQ>("KNNKSM");
    add<KNNKBB>("KNNKSS");
    add<KNNKNP>("KNNKNP");
    add<KNNKNQ>("KNNKNM");
    add<KNNKNB>("KNNKNS");
  }
}


/// Mate with KX vs K. This function is used to evaluate positions with
/// king and plenty of material vs a lone king. It simply gives the
/// attacking side a bonus for driving the defending king towards the edge
/// of the board, and for keeping the distance between the two kings small.
template<>
Value Endgame<KXK>::operator()(const Position& pos) const {

  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  // Stalemate detection with lone king
  if (pos.side_to_move() == weakSide && !MoveList<LEGAL>(pos).size())
      return VALUE_DRAW;

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToEdges[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<BISHOP>(strongSide) >= 1)
    result += PushToEdges[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide) >= 1)
    result += PushToEdges[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  if (   (pos.count<ROOK>(strongSide) >= 1)
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<BISHOP>(strongSide) >= 1))
      || ((pos.count<ROOK>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<BISHOP>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<BISHOP>(strongSide) >= 1) && (pos.count<KNIGHT>(strongSide) >= 1))
      || (pos.count<BISHOP>(strongSide) == 2)
      || ((pos.count<BISHOP>(strongSide) >= 1) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count<KNIGHT>(strongSide) == 1) && (pos.count<QUEEN>(strongSide) >= 2))
      || ((pos.count<KNIGHT>(strongSide) == 2) && (pos.count<QUEEN>(strongSide) >= 1))
      || ((pos.count< QUEEN>(strongSide) >= 3)
        && ( DarkSquares & pos.pieces(strongSide, QUEEN))
        && (~DarkSquares & pos.pieces(strongSide, QUEEN))))
        result = std::min(result + VALUE_KNOWN_WIN, VALUE_MATE_IN_MAX_PLY - 1);
  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if ((pos.count< QUEEN>(strongSide) >= 3)
          && !pos.count<ROOK>(strongSide)
          && !pos.count<KNIGHT>(strongSide)
          && !pos.count<BISHOP>(strongSide)
          &&(!dark || !light))
          return VALUE_DRAW; // pr0rp
  }

  return strongSide == pos.side_to_move() ? result : -result;
}


/// KQsPs vs K.
template<>
Value Endgame<KQsPsK>::operator()(const Position& pos) const {

  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - pos.count<PAWN>(weakSide) * PawnValueEg;

  if ( pos.count<QUEEN>(strongSide) >= 3
      && ( DarkSquares & pos.pieces(strongSide, QUEEN))
      && (~DarkSquares & pos.pieces(strongSide, QUEEN)))
      result += PushToEdges[loserKSq];
  else if (pos.count<QUEEN>(strongSide) + pos.count<PAWN>(strongSide) < 3)
      return VALUE_DRAW;
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


//KXKRR redghost begin
template<>
Value Endgame<KXKRR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                - 2 * RookValueMg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRXKRR
template<>
Value Endgame<KRXKRR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                - 2 * RookValueMg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRRKR
template<>
Value Endgame<KRRKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 7 * RookValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNBQKR
template<>
Value Endgame<KRNBQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 7 * RookValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNNKR
template<>
Value Endgame<KRNNKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 7 * RookValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNBKR
template<>
Value Endgame<KRNBKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 7 * RookValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNQKR
template<>
Value Endgame<KRNQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 7 * RookValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBBKR
template<>
Value Endgame<KRBBKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 7 * RookValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBQKR
template<>
Value Endgame<KRBQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square rookSq = pos.square<ROOK>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);
  Square brsq = pos.square<ROOK>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + 7 * RookValueEg
                + PushAway[distance(loserKSq, brsq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if (pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if (pos.count<QUEEN>(strongSide) || pos.attacks_from<QUEEN>(queenSq) || pos.attacks_from<ROOK>(rookSq))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];
  if (pos.count<BISHOP>(strongSide) || pos.attacks_from<BISHOP>(bishopSq) || pos.attacks_from<ROOK>(rookSq))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRQQQKR
template<>
Value Endgame<KRQQQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + 7 * RookValueEg
                + PushToCorn[loserKSq];

  if (pos.attacks_from<KING>(winnerKSq) || pos.attacks_from<QUEEN>(queenSq))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)]
            + PushClose[distance(winnerKSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRQQKR
template<>
Value Endgame<KRQQKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square rookSq = pos.square<ROOK>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);
  Square brsq = pos.square<ROOK>(weakSide);

  if (opposite_colors(queenSq, SQ_A1))
  {
      winnerKSq = ~(winnerKSq);
      loserKSq  = ~(loserKSq);
  }

  Value result =  pos.non_pawn_material(strongSide)
                - RookValueEg
                + PushAway[distance(loserKSq, brsq)]
                + PushToCorn[loserKSq];

  if (pos.count<KING>(strongSide))
    result += PushToQueenCorners[opposite_colors(queenSq, SQ_A1) ? ~(loserKSq) : loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if (pos.count<QUEEN>(strongSide) || pos.attacks_from<QUEEN>(queenSq) || pos.attacks_from<ROOK>(rookSq))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if ((pos.count<QUEEN>(strongSide) == 2)
          && (pos.count<ROOK>(strongSide) == 1)
          && (pos.count<ROOK>(weakSide) == 1)
          && !pos.count<KNIGHT>(strongSide)
          && !pos.count<BISHOP>(strongSide)
          && (dark && light))  // both color draw
        result += PushToEdges[loserKSq]
                - 6 * PawnValueMg;
  }
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRKQ
template<>
Value Endgame<KRKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToEdges[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQQKQ
template<>
Value Endgame<KQQQKQ>::operator()(const Position& pos) const {

  Square loserKSq = pos.square<KING>(weakSide);
  Square bqsq = pos.square<QUEEN>(weakSide);
  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);

  Value result =  pos.non_pawn_material(strongSide)
                - QueenValueEg
                - PawnValueEg / 2
                + PushAway[distance(loserKSq, bqsq)]
                + PushToCorn[loserKSq];

  if (pos.attacks_from<KING>(winnerKSq) || pos.attacks_from<QUEEN>(queenSq))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)]
            + PushClose[distance(winnerKSq, loserKSq)];
  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if (!dark || !light)
          return VALUE_DRAW; // same color draw
  }
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQKQ
template<>
Value Endgame<KBQKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square bqsq = pos.square<BISHOP>(weakSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + PushAway[distance(loserKSq, bqsq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBBKQ
template<>
Value Endgame<KBBKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBQKQ
template<>
Value Endgame<KNBQKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNQKQ
template<>
Value Endgame<KNNQKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQKQ
template<>
Value Endgame<KNQQKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBKQ
template<>
Value Endgame<KNBKQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRKB
template<>
Value Endgame<KRKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBQKB
template<>
Value Endgame<KNBQKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQKB
template<>
Value Endgame<KNQQKB>::operator()(const Position& pos) const {

  Square loserKSq = pos.square<KING>(weakSide);
  Square loserBSq = pos.square<BISHOP>(weakSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square winnerKSq = pos.square<KING>(strongSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushAway[distance(loserKSq, loserBSq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? ~loserKSq : loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? ~loserKSq : loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? ~loserKSq : loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? ~loserKSq : loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQQKB
template<>
Value Endgame<KBQQKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);
  Square bbsq = pos.square<BISHOP>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if ((pos.count< QUEEN>(strongSide) == 2)
          && !pos.count<ROOK>(strongSide)
          && !pos.count<KNIGHT>(strongSide)
          && (pos.count<BISHOP>(strongSide) == 1)
          && (!dark || !light)) // same color
          result += PushToOpposingSideEdges[strongSide == BLACK ? loserKSq : ~loserKSq]
                  + PushAway[distance(loserKSq, bbsq)]
                  - BishopValueEg;
  }

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNQKB
template<>
Value Endgame<KNNQKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBBQKB
template<>
Value Endgame<KBBQKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBKB
template<>
Value Endgame<KNBKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQQQKB
template<>
Value Endgame<KQQQQKB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - BishopValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if (!dark || !light)
          return VALUE_DRAW; // same color draw
  }
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQQQQKR
template<>
Value Endgame<KBQQQQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBBQQKR
template<>
Value Endgame<KBBQQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQQQKR
template<>
Value Endgame<KNQQQQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNQQKR
template<>
Value Endgame<KNNQQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBBNKR
template<>
Value Endgame<KBBNKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBBQKR
template<>
Value Endgame<KNBBQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNBKR
template<>
Value Endgame<KNNBKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - RookValueEg
                - BishopValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNBQKR
template<>
Value Endgame<KNNBQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBQQKR
template<>
Value Endgame<KNBQQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQQQQKR
template<>
Value Endgame<KQQQQQKR>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNBQKN
template<>
Value Endgame<KRNBQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNBKN
template<>
Value Endgame<KRNBKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNQKN
template<>
Value Endgame<KRNQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBQKN
template<>
Value Endgame<KRBQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRQKN
template<>
Value Endgame<KRQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToCorn[loserKSq];

  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBKN
template<>
Value Endgame<KRBKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNKN
template<>
Value Endgame<KRNKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRRKN
template<>
Value Endgame<KRRKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQQQKN
template<>
Value Endgame<KBQQQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQQKN
template<>
Value Endgame<KNQQQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBBQKN
template<>
Value Endgame<KBBQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBQKN
template<>
Value Endgame<KNBQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNQQKN
template<>
Value Endgame<KNNQQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNBKN
template<>
Value Endgame<KNNBKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBBKN
template<>
Value Endgame<KNBBKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQQQQKN
template<>
Value Endgame<KQQQQQKN>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if (!dark || !light)
          return VALUE_DRAW; // same color draw
  }
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNQKRQ
template<>
Value Endgame<KRNQKRQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - RookValueEg
                - QueenValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBQQKRQ
template<>
Value Endgame<KRBQQKRQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - RookValueEg
                - QueenValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNQQKRB
template<>
Value Endgame<KRNQQKRB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - RookValueEg
                - BishopValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Mate with KBQ vs K.
template<>
Value Endgame<KBQK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  BishopValueEg
                + QueenValueEg
                + 4 * RookValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<BISHOP>(strongSide) || pos.attacks_from<BISHOP>(bishopSq))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide) || pos.attacks_from<QUEEN>(queenSq))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


/// Mate with KNQ vs K.
template<>
Value Endgame<KNQK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq  = pos.square<KING>(weakSide);
  Square queenSq   = pos.square<QUEEN>(strongSide);
  Square knightSq  = pos.square<KNIGHT>(strongSide);

  // tries to drive toward corners A1 or H8. If we have a
  // queen that cannot reach the above squares, we flip the kings in order
  // to drive the enemy toward corners A8 or H1.
  if (opposite_colors(queenSq, SQ_A1))
  {
      winnerKSq = ~(winnerKSq);
      loserKSq  = ~(loserKSq);
      knightSq  = ~(knightSq);
  }

  // Weak king not in queen's corner could be draw
  if (distance(SQ_A1, loserKSq) >= 4 && distance(SQ_H8, loserKSq) >= 4)
      return Value(PushClose[distance(winnerKSq, loserKSq)]);

  // King and knight on the winnng square ?
  Value winValue = (  distance(winnerKSq, distance(SQ_A1, loserKSq) < 4 ? SQ_A1 : SQ_H8) <= 4
                    && popcount(pos.attacks_from<KING>(loserKSq) & pos.attacks_from<KNIGHT>(knightSq)) > 0) ?
                    PawnValueMg : VALUE_ZERO;

  Value result =  winValue
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToQueenCorners[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPK
template<>
Value Endgame<KNPK>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


/// KR vs KN. The attacking side has slightly better winning chances than
/// in KR vs KB, particularly if the king and the knight are far apart.
template<>
Value Endgame<KRKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Square bksq = pos.square<KING>(weakSide);
  Square bnsq = pos.square<KNIGHT>(weakSide);
  Value result = Value(PushToEdges[bksq] + PushAway[distance(bksq, bnsq)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRQKBQ
template<>
Value Endgame<KRQKBQ>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - QueenValueEg
                - BishopValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<ROOK>(strongSide))
    result += PushToCorn[loserKSq];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQQKR
template<>
Value Endgame<KNQQQKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueEg + 3 * QueenValueEg, 0));
  assert(verify_material(pos, weakSide, RookValueEg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  PawnValueEg
                - PawnValueEg
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToCorn[loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToCorn[loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];
    else
    {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if (!dark || !light) // same color draw
          result += PushToCorn[loserKSq]
                  - PawnValueEg;
    }
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQQQKR
template<>
Value Endgame<KBQQQKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueEg + 3 * QueenValueEg, 0));
  assert(verify_material(pos, weakSide, RookValueEg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  PawnValueEg
                - PawnValueEg
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNKR
template<>
Value Endgame<KRNKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBKR
template<>
Value Endgame<KRBKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + BishopValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRQKR
template<>
Value Endgame<KRQKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRPKR
template<>
Value Endgame<KRPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = VALUE_DRAW;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPKP
template<>
Value Endgame<KNPKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, PawnValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 40;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPKQ
template<>
Value Endgame<KNPKQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 40;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPKQQ
template<>
Value Endgame<KNPKQQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 50;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQKP
template<>
Value Endgame<KNQKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, PawnValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq  = pos.square<KING>(weakSide);
  Square queenSq   = pos.square<QUEEN>(strongSide);
  Square knightSq  = pos.square<KNIGHT>(strongSide);

  // tries to drive toward corners A1 or H8. If we have a
  // queen that cannot reach the above squares, we flip the kings in order
  // to drive the enemy toward corners A8 or H1.
  if (opposite_colors(queenSq, SQ_A1))
  {
      winnerKSq = ~(winnerKSq);
      loserKSq  = ~(loserKSq);
      knightSq  = ~(knightSq);
  }

  // Weak king not in queen's corner could be draw
  if (distance(SQ_A1, loserKSq) >= 4 && distance(SQ_H8, loserKSq) >= 4)
      return Value(PushClose[distance(winnerKSq, loserKSq)]) -25;

  // King and knight on the winnng square ? 
  Value winValue = (  distance(winnerKSq, distance(SQ_A1, loserKSq) < 4 ? SQ_A1 : SQ_H8) <= 4
                    && popcount(pos.attacks_from<KING>(loserKSq) & pos.attacks_from<KNIGHT>(knightSq)) > 0) ?
                    PawnValueMg : VALUE_ZERO;

  Value result =  winValue
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToQueenCorners[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQKQ
template<>
Value Endgame<KNQKQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq  = pos.square<KING>(weakSide);
  Square queenSq   = pos.square<QUEEN>(strongSide);
  Square knightSq  = pos.square<KNIGHT>(strongSide);

  // tries to drive toward corners A1 or H8. If we have a
  // queen that cannot reach the above squares, we flip the kings in order
  // to drive the enemy toward corners A8 or H1.
  if (opposite_colors(queenSq, SQ_A1))
  {
      winnerKSq = ~(winnerKSq);
      loserKSq  = ~(loserKSq);
      knightSq  = ~(knightSq);
  }

  // Weak king not in queen's corner could be draw
  if (distance(SQ_A1, loserKSq) >= 4 && distance(SQ_H8, loserKSq) >= 4)
      return Value(PushClose[distance(winnerKSq, loserKSq)]) - 25;

  // King and knight on the winnng square ? 
  Value winValue = (  distance(winnerKSq, distance(SQ_A1, loserKSq) < 4 ? SQ_A1 : SQ_H8) <= 4
                    && popcount(pos.attacks_from<KING>(loserKSq) & pos.attacks_from<KNIGHT>(knightSq)) > 0) ?
                    PawnValueMg : VALUE_ZERO;

  Value result =  winValue
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToQueenCorners[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQKB
template<>
Value Endgame<KNQKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq  = pos.square<KING>(weakSide);
  Square queenSq   = pos.square<QUEEN>(strongSide);
  Square knightSq  = pos.square<KNIGHT>(strongSide);

  // tries to drive toward corners A1 or H8. If we have a
  // queen that cannot reach the above squares, we flip the kings in order
  // to drive the enemy toward corners A8 or H1.
  if (opposite_colors(queenSq, SQ_A1))
  {
      winnerKSq = ~(winnerKSq);
      loserKSq  = ~(loserKSq);
      knightSq  = ~(knightSq);
  }

  // Weak king not in queen's corner could be draw
  if (distance(SQ_A1, loserKSq) >= 4 && distance(SQ_H8, loserKSq) >= 4)
      return Value(PushClose[distance(winnerKSq, loserKSq)]) - 25;

  // King and knight on the winnng square ? 
  Value winValue = (  distance(winnerKSq, distance(SQ_A1, loserKSq) < 4 ? SQ_A1 : SQ_H8) <= 4
                    && popcount(pos.attacks_from<KING>(loserKSq) & pos.attacks_from<KNIGHT>(knightSq)) > 0) ?
                    PawnValueMg : VALUE_ZERO;

  Value result =  winValue
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToQueenCorners[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPKB
template<>
Value Endgame<KNPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 40;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPKN
template<>
Value Endgame<KNPKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 40;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQKN
template<>
Value Endgame<KNQKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQKQQ
template<>
Value Endgame<KNQKQQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + QueenValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square loserKSq  = pos.square<KING>(weakSide);
  Square queenSq   = pos.square<QUEEN>(strongSide);
  Square knightSq  = pos.square<KNIGHT>(strongSide);

  // tries to drive toward corners A1 or H8. If we have a
  // queen that cannot reach the above squares, we flip the kings in order
  // to drive the enemy toward corners A8 or H1.
  if (opposite_colors(queenSq, SQ_A1))
  {
      winnerKSq = ~(winnerKSq);
      loserKSq  = ~(loserKSq);
      knightSq  = ~(knightSq);
  }

  // Weak king not in queen's corner could be draw
  if (distance(SQ_A1, loserKSq) >= 4 && distance(SQ_H8, loserKSq) >= 4)
      return Value(PushClose[distance(winnerKSq, loserKSq)]) - 30;

  // King and knight on the winnng square ? 
  Value winValue = (  distance(winnerKSq, distance(SQ_A1, loserKSq) < 4 ? SQ_A1 : SQ_H8) <= 4
                    && popcount(pos.attacks_from<KING>(loserKSq) & pos.attacks_from<KNIGHT>(knightSq)) > 0) ?
                    PawnValueMg : VALUE_ZERO;

  Value result =  winValue
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToQueenCorners[loserKSq];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRQPKR
template<>
Value Endgame<KRQPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 25;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRPPKR
template<>
Value Endgame<KRPPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


/// KNN vs KP. Very drawish, but there are some mate opportunities if we can
//  press the weakSide King to a corner before the pawn advances too much.
template<>
Value Endgame<KNNKP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, 2 * KnightValueMg, 0));
  assert(verify_material(pos, weakSide, VALUE_ZERO, 1));

  Value result =      PawnValueEg
               +  2 * PushToEdges[pos.square<KING>(weakSide)]
               - 10 * relative_rank(weakSide, pos.square<PAWN>(weakSide));

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKQ
template<>
Value Endgame<KNNKQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKB
template<>
Value Endgame<KNNKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKN
template<>
Value Endgame<KNNKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKR
template<>
Value Endgame<KNNKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQQKN
template<>
Value Endgame<KBQQKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  BishopValueEg
                + 2 * QueenValueEg
                - KnightValueEg
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  {
      bool dark  =  DarkSquares & pos.pieces(strongSide, QUEEN);
      bool light = ~DarkSquares & pos.pieces(strongSide, QUEEN);

      if ((pos.count< QUEEN>(strongSide) == 2)
          && !pos.count<ROOK>(strongSide)
          && !pos.count<KNIGHT>(strongSide)
          && (pos.count<BISHOP>(strongSide) == 1)
          && (!dark || !light)) // same color
          result += PushToOpposingSideEdges[strongSide == BLACK ? loserKSq : ~loserKSq]
                  - QueenValueEg;
  }

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQKN
template<>
Value Endgame<KNQQKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Square bksq = pos.square<KING>(weakSide);
  Square bnsq = pos.square<KNIGHT>(weakSide);
  Value result = Value(PushToEdges[bksq] + PushAway[distance(bksq, bnsq)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQKBQ
template<>
Value Endgame<KNQQKBQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBQKR
template<>
Value Endgame<KNBQKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + BishopValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNBQQKRQ
template<>
Value Endgame<KNBQQKRQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + BishopValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRQQKRQ
template<>
Value Endgame<KRQQKRQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNQKRB
template<>
Value Endgame<KRNQKRB>::operator()(const Position& pos) const {

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  pos.non_pawn_material(strongSide)
                + pos.count<PAWN>(strongSide) * PawnValueEg
                - QueenValueEg
                - RookValueEg
                - BishopValueEg
                + PushToOpposingSideEdges[strongSide == BLACK ? loserKSq : ~loserKSq];

  if(pos.count<KING>(strongSide))
    result += PushToOpposingSideEdges[strongSide == BLACK ? loserKSq : ~loserKSq]
            + PushClose[distance(winnerKSq, loserKSq)];
  if(pos.count<KNIGHT>(strongSide))
    result += PushToOpposingSideEdges[strongSide == BLACK ? loserKSq : ~loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == BLACK ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBQKRQ
template<>
Value Endgame<KRBQKRQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + BishopValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg + QueenValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square bishopSq = pos.square<BISHOP>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);

  Value result =  PawnValueMg
                + PushClose[distance(winnerKSq, loserKSq)]
                + PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq];

  if(pos.count<BISHOP>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(bishopSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToOpposingSideEdges[strongSide == WHITE ? loserKSq : ~loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQQKBQ
template<>
Value Endgame<KBQQKBQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 100;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQPKBQ
template<>
Value Endgame<KBQPKBQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 50;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBPPKBQ
template<>
Value Endgame<KBPPKBQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQQKQQ
template<>
Value Endgame<KQQQKQQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQKQ
template<>
Value Endgame<KQQKQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg, 0));

  Value result = VALUE_DRAW;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQKB
template<>
Value Endgame<KBQKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 50;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBPKB
template<>
Value Endgame<KBPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQQKB
template<>
Value Endgame<KQQQKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 75;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KQQPKB
template<>
Value Endgame<KQQPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 50;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KQPPKB
template<>
Value Endgame<KQPPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, QueenValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 25;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KPPPKB
template<>
Value Endgame<KPPPKB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, PawnValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQPKN
template<>
Value Endgame<KNQPKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 25;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPPKN
template<>
Value Endgame<KNPPKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQPKN
template<>
Value Endgame<KBQPKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 25;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBPPKN
template<>
Value Endgame<KBPPKN>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRNPKRB
template<>
Value Endgame<KRNPKRB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KRBPKRQ
template<>
Value Endgame<KRBPKRQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, RookValueMg + BishopValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPPPKR
template<>
Value Endgame<KNPPPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQPPKR
template<>
Value Endgame<KNQPPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 25;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQPKR
template<>
Value Endgame<KNQQPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 50;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBPPPKR
template<>
Value Endgame<KBPPPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + PawnValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQPPKR
template<>
Value Endgame<KBQPPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 25;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KBQQPKR
template<>
Value Endgame<KBQQPKR>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, BishopValueMg + QueenValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, RookValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 50;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQKQQ
template<>
Value Endgame<KNQQKQQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + QueenValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);
  Square bqsq = pos.square<QUEEN>(weakSide);

  Value result =  PawnValueMg / 4
                + PushAway[distance(loserKSq, bqsq)]
                + PushToCorn[loserKSq];

  if (pos.attacks_from<KING>(winnerKSq) || pos.attacks_from<QUEEN>(queenSq) || pos.attacks_from<KNIGHT>(knightSq))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)]
            + PushClose[distance(winnerKSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQPKQQ
template<>
Value Endgame<KNQPKQQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               + 50;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNPPKQQ
template<>
Value Endgame<KNPPKQQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + PawnValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNPKNP
template<>
Value Endgame<KNNPKNP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + PawnValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 40;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNPKNQ
template<>
Value Endgame<KNNPKNQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 30;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNPKNB
template<>
Value Endgame<KNNPKNB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg + PawnValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)])
               - 20;
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNQQQKNQ
template<>
Value Endgame<KNQQQKNQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + QueenValueMg + QueenValueMg + QueenValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + QueenValueMg, 0));

  Square winnerKSq = pos.square<KING>(strongSide);
  Square knightSq = pos.square<KNIGHT>(strongSide);
  Square queenSq = pos.square<QUEEN>(strongSide);
  Square loserKSq = pos.square<KING>(weakSide);
  Square bnsq = pos.square<KNIGHT>(weakSide);

  Value result =  2 * QueenValueEg
                + pos.count<PAWN>(strongSide) * PawnValueEg
                + PushAway[distance(loserKSq, bnsq)]
                + PushToCorn[loserKSq]
                + PushClose[distance(winnerKSq, loserKSq)];

  if(pos.count<KNIGHT>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(knightSq, loserKSq)];
  if(pos.count<QUEEN>(strongSide))
    result += PushToCorn[loserKSq]
            + PushWin[distance(queenSq, loserKSq)];

  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKPP
template<>
Value Endgame<KNNKPP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, PawnValueMg + PawnValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKQP
template<>
Value Endgame<KNNKQP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + PawnValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKQQ
template<>
Value Endgame<KNNKQQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, QueenValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKBP
template<>
Value Endgame<KNNKBP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg + PawnValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKBQ
template<>
Value Endgame<KNNKBQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKBB
template<>
Value Endgame<KNNKBB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, BishopValueMg + BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKNP
template<>
Value Endgame<KNNKNP>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + PawnValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKNQ
template<>
Value Endgame<KNNKNQ>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + QueenValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


//KNNKNB redghost end.
template<>
Value Endgame<KNNKNB>::operator()(const Position& pos) const {

  assert(verify_material(pos, strongSide, KnightValueMg + KnightValueMg, 0));
  assert(verify_material(pos, weakSide, KnightValueMg + BishopValueMg, 0));

  Value result = Value(PushToEdges[pos.square<KING>(weakSide)]);
  return strongSide == pos.side_to_move() ? result : -result;
}


/// Some cases of trivial draws  redghost end.
template<> Value Endgame<KNNK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQQK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQPK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KPPK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KBK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KPK>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNKB>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNKQ>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KBKQ>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KNKP>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KBKP>::operator()(const Position&) const { return VALUE_DRAW; }

template<> Value Endgame<KQKP>::operator()(const Position&) const { return VALUE_DRAW; }

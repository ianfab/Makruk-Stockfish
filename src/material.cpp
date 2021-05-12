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

#include <algorithm> // For std::min
#include <cassert>
#include <cstring>   // For std::memset

#include "material.h"
#include "thread.h"

using namespace std;

namespace {

  // Polynomial material imbalance parameters

  constexpr int QuadraticOurs[][PIECE_TYPE_NB] = {
    //            OUR PIECES
    // Q-pair pawn queen bishop knight rook
    {1439                                }, // Q-pair
    {  40,   39                          }, // Pawn
    {   0,   69,    0                    }, // Queen
    {   0,  104,  133,   138             }, // Bishop
    {  32,  255,    2,     4,    2       }, // Knight      OUR PIECES
    { -26,   -2,   52,   110,   47, -208 }  // Rook
  };

  constexpr int QuadraticTheirs[][PIECE_TYPE_NB] = {
    //           THEIR PIECES
    // Q-pair pawn queen bishop knight rook
    {   0                                }, // Q-pair
    {  36,    0                          }, // Pawn
    {  40,   50,     0                   }, // Queen
    {  59,   65,    25,     0            }, // Bishop
    {   9,   63,     7,    42,    0      }, // Knight      OUR PIECES
    {  46,   39,    -8,   -24,   24,   0 }  // Rook
  };

  // Endgame evaluation and scaling functions are accessed directly and not through
  // the function maps because they correspond to more than one material hash key. redghost
  Endgame<KXK>      EvaluateKXK[]      = { Endgame<KXK>(WHITE),      Endgame<KXK>(BLACK) };
  Endgame<KQsPsK>   EvaluateKQsPsK[]   = { Endgame<KQsPsK>(WHITE),   Endgame<KQsPsK>(BLACK) };
  Endgame<KXKRR>    EvaluateKXKRR[]    = { Endgame<KXKRR>(WHITE),    Endgame<KXKRR>(BLACK) };
  Endgame<KRXKRR>   EvaluateKRXKRR[]   = { Endgame<KRXKRR>(WHITE),   Endgame<KRXKRR>(BLACK) };
  Endgame<KRRKR>    EvaluateKRRKR[]    = { Endgame<KRRKR>(WHITE),    Endgame<KRRKR>(BLACK) };
  Endgame<KRNBQKR>  EvaluateKRNBQKR[]  = { Endgame<KRNBQKR>(WHITE),  Endgame<KRNBQKR>(BLACK) };
  Endgame<KRNNKR>   EvaluateKRNNKR[]   = { Endgame<KRNNKR>(WHITE),   Endgame<KRNNKR>(BLACK) };
  Endgame<KRNBKR>   EvaluateKRNBKR[]   = { Endgame<KRNBKR>(WHITE),   Endgame<KRNBKR>(BLACK) };
  Endgame<KRNQKR>   EvaluateKRNQKR[]   = { Endgame<KRNQKR>(WHITE),   Endgame<KRNQKR>(BLACK) };
  Endgame<KRBBKR>   EvaluateKRBBKR[]   = { Endgame<KRBBKR>(WHITE),   Endgame<KRBBKR>(BLACK) };
  Endgame<KRBQKR>   EvaluateKRBQKR[]   = { Endgame<KRBQKR>(WHITE),   Endgame<KRBQKR>(BLACK) };
  Endgame<KRQQQKR>  EvaluateKRQQQKR[]  = { Endgame<KRQQQKR>(WHITE),  Endgame<KRQQQKR>(BLACK) };
  Endgame<KRKQ>     EvaluateKRKQ[]     = { Endgame<KRKQ>(WHITE),     Endgame<KRKQ>(BLACK) };
  Endgame<KQQQKQ>   EvaluateKQQQKQ[]   = { Endgame<KQQQKQ>(WHITE),   Endgame<KQQQKQ>(BLACK) };
  Endgame<KBQKQ>    EvaluateKBQKQ[]    = { Endgame<KBQKQ>(WHITE),    Endgame<KBQKQ>(BLACK) };
  Endgame<KBBKQ>    EvaluateKBBKQ[]    = { Endgame<KBBKQ>(WHITE),    Endgame<KBBKQ>(BLACK) };
  Endgame<KNBQKQ>   EvaluateKNBQKQ[]   = { Endgame<KNBQKQ>(WHITE),   Endgame<KNBQKQ>(BLACK) };
  Endgame<KNNQKQ>   EvaluateKNNQKQ[]   = { Endgame<KNNQKQ>(WHITE),   Endgame<KNNQKQ>(BLACK) };
  Endgame<KNQQKQ>   EvaluateKNQQKQ[]   = { Endgame<KNQQKQ>(WHITE),   Endgame<KNQQKQ>(BLACK) };
  Endgame<KNBKQ>    EvaluateKNBKQ[]    = { Endgame<KNBKQ>(WHITE),    Endgame<KNBKQ>(BLACK) };
  Endgame<KRKB>     EvaluateKRKB[]     = { Endgame<KRKB>(WHITE),     Endgame<KRKB>(BLACK) };
  Endgame<KNBQKB>   EvaluateKNBQKB[]   = { Endgame<KNBQKB>(WHITE),   Endgame<KNBQKB>(BLACK) };
  Endgame<KNQQKB>   EvaluateKNQQKB[]   = { Endgame<KNQQKB>(WHITE),   Endgame<KNQQKB>(BLACK) };
  Endgame<KBQQKB>   EvaluateKBQQKB[]   = { Endgame<KBQQKB>(WHITE),   Endgame<KBQQKB>(BLACK) };
  Endgame<KNNQKB>   EvaluateKNNQKB[]   = { Endgame<KNNQKB>(WHITE),   Endgame<KNNQKB>(BLACK) };
  Endgame<KBBQKB>   EvaluateKBBQKB[]   = { Endgame<KBBQKB>(WHITE),   Endgame<KBBQKB>(BLACK) };
  Endgame<KNBKB>    EvaluateKNBKB[]    = { Endgame<KNBKB>(WHITE),    Endgame<KNBKB>(BLACK) };
  Endgame<KQQQQKB>  EvaluateKQQQQKB[]  = { Endgame<KQQQQKB>(WHITE),  Endgame<KQQQQKB>(BLACK) };
  Endgame<KBQQQQKR> EvaluateKBQQQQKR[] = { Endgame<KBQQQQKR>(WHITE), Endgame<KBQQQQKR>(BLACK) };
  Endgame<KBBQQKR>  EvaluateKBBQQKR[]  = { Endgame<KBBQQKR>(WHITE),  Endgame<KBBQQKR>(BLACK) };
  Endgame<KNQQQQKR> EvaluateKNQQQQKR[] = { Endgame<KNQQQQKR>(WHITE), Endgame<KNQQQQKR>(BLACK) };
  Endgame<KNNQQKR>  EvaluateKNNQQKR[]  = { Endgame<KNNQQKR>(WHITE),  Endgame<KNNQQKR>(BLACK) };
  Endgame<KBBNKR>   EvaluateKBBNKR[]   = { Endgame<KBBNKR>(WHITE),   Endgame<KBBNKR>(BLACK) };
  Endgame<KNBBQKR>  EvaluateKNBBQKR[]  = { Endgame<KNBBQKR>(WHITE),  Endgame<KNBBQKR>(BLACK) };
  Endgame<KNNBKR>   EvaluateKNNBKR[]   = { Endgame<KNNBKR>(WHITE),   Endgame<KNNBKR>(BLACK) };
  Endgame<KNNBQKR>  EvaluateKNNBQKR[]  = { Endgame<KNNBQKR>(WHITE),  Endgame<KNNBQKR>(BLACK) };
  Endgame<KNBQQKR>  EvaluateKNBQQKR[]  = { Endgame<KNBQQKR>(WHITE),  Endgame<KNBQQKR>(BLACK) };
  Endgame<KQQQQQKR> EvaluateKQQQQQKR[] = { Endgame<KQQQQQKR>(WHITE), Endgame<KQQQQQKR>(BLACK) };
  Endgame<KRNBQKN>  EvaluateKRNBQKN[]  = { Endgame<KRNBQKN>(WHITE),  Endgame<KRNBQKN>(BLACK) };
  Endgame<KRNBKN>   EvaluateKRNBKN[]   = { Endgame<KRNBKN>(WHITE),   Endgame<KRNBKN>(BLACK) };
  Endgame<KRNQKN>   EvaluateKRNQKN[]   = { Endgame<KRNQKN>(WHITE),   Endgame<KRNQKN>(BLACK) };
  Endgame<KRBQKN>   EvaluateKRBQKN[]   = { Endgame<KRBQKN>(WHITE),   Endgame<KRBQKN>(BLACK) };
  Endgame<KRQKN>    EvaluateKRQKN[]    = { Endgame<KRQKN>(WHITE),    Endgame<KRQKN>(BLACK) };
  Endgame<KRBKN>    EvaluateKRBKN[]    = { Endgame<KRBKN>(WHITE),    Endgame<KRBKN>(BLACK) };
  Endgame<KRNKN>    EvaluateKRNKN[]    = { Endgame<KRNKN>(WHITE),    Endgame<KRNKN>(BLACK) };
  Endgame<KRRKN>    EvaluateKRRKN[]    = { Endgame<KRRKN>(WHITE),    Endgame<KRRKN>(BLACK) };
  Endgame<KBQQQKN>  EvaluateKBQQQKN[]  = { Endgame<KBQQQKN>(WHITE),  Endgame<KBQQQKN>(BLACK) };
  Endgame<KNQQQKN>  EvaluateKNQQQKN[]  = { Endgame<KNQQQKN>(WHITE),  Endgame<KNQQQKN>(BLACK) };
  Endgame<KBBQKN>   EvaluateKBBQKN[]   = { Endgame<KBBQKN>(WHITE),   Endgame<KBBQKN>(BLACK) };
  Endgame<KNBQKN>   EvaluateKNBQKN[]   = { Endgame<KNBQKN>(WHITE),   Endgame<KNBQKN>(BLACK) };
  Endgame<KNNQQKN>  EvaluateKNNQQKN[]  = { Endgame<KNNQQKN>(WHITE),  Endgame<KNNQQKN>(BLACK) };
  Endgame<KNNBKN>   EvaluateKNNBKN[]   = { Endgame<KNNBKN>(WHITE),   Endgame<KNNBKN>(BLACK) };
  Endgame<KNBBKN>   EvaluateKNBBKN[]   = { Endgame<KNBBKN>(WHITE),   Endgame<KNBBKN>(BLACK) };
  Endgame<KQQQQQKN> EvaluateKQQQQQKN[] = { Endgame<KQQQQQKN>(WHITE), Endgame<KQQQQQKN>(BLACK) };
  Endgame<KRNQKRQ>  EvaluateKRNQKRQ[]  = { Endgame<KRNQKRQ>(WHITE),  Endgame<KRNQKRQ>(BLACK) };
  Endgame<KRBQQKRQ> EvaluateKRBQQKRQ[] = { Endgame<KRBQQKRQ>(WHITE), Endgame<KRBQQKRQ>(BLACK) };
  Endgame<KRNQQKRB> EvaluateKRNQQKRB[] = { Endgame<KRNQQKRB>(WHITE), Endgame<KRNQQKRB>(BLACK) };
  Endgame<KRQKBQ>   EvaluateKRQKBQ[]   = { Endgame<KRQKBQ>(WHITE),   Endgame<KRQKBQ>(BLACK) };
  
  // Helper used to detect a given material distribution redghost
  bool is_KXK(const Position& pos, Color us) {
    return  !more_than_one(pos.pieces(~us))
          && (pos.count<PAWN>(us) || !pos.count<PAWN>(us))
          && pos.non_pawn_material(us) >= BishopValueMg + QueenValueMg;
  }

  bool is_KQsPsK(const Position& pos, Color us) {
    return   (pos.count<QUEEN >(us) || pos.count<PAWN>(us))
          && !pos.count<ROOK  >(us)
          && !pos.count<BISHOP>(us)
          && !pos.count<KNIGHT>(us)
          && (pos.count<PAWN>(~us) || !pos.count<PAWN>(~us))
          && !pos.count<ROOK>(~us)
          && !pos.count<BISHOP>(~us)
          && !pos.count<KNIGHT>(~us)
          && !pos.count<QUEEN>(~us);
  }

  bool is_KXKRR(const Position& pos, Color us) {
    return   !pos.count<PAWN>(us)
          && !pos.count<ROOK>(us)
          && (pos.count<KNIGHT>(us) || !pos.count<KNIGHT>(us))
          && (pos.count<BISHOP>(us) || !pos.count<BISHOP>(us))
          && (pos.count<QUEEN>(us) || !pos.count<QUEEN>(us))
          && !pos.count<PAWN>(~us)
          && pos.count<ROOK>(~us) == 2
          && !pos.count<BISHOP>(~us)
          && !pos.count<KNIGHT>(~us)
          && !pos.count<QUEEN>(~us)
          && pos.non_pawn_material(us) - pos.non_pawn_material(~us) >= PawnValueMg;
  }

  bool is_KRXKRR(const Position& pos, Color us) {
    return   !pos.count<PAWN>(us)
          && pos.count<ROOK>(us) == 1
          && (pos.count<KNIGHT>(us) || !pos.count<KNIGHT>(us))
          && (pos.count<BISHOP>(us) || !pos.count<BISHOP>(us))
          && (pos.count<QUEEN>(us) || !pos.count<QUEEN>(us))
          && !pos.count<PAWN>(~us)
          && pos.count<ROOK>(~us) == 2
          && !pos.count<BISHOP>(~us)
          && !pos.count<KNIGHT>(~us)
          && !pos.count<QUEEN>(~us)
          && pos.non_pawn_material(us) - pos.non_pawn_material(~us) >= PawnValueMg;
  }

  bool is_KRRKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + RookValueMg
          && pos.count<ROOK>(us) == 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRNBQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + BishopValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRNNKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + KnightValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRNBKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + BishopValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRNQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRBBKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + BishopValueMg + BishopValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<BISHOP>(us) == 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRBQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + BishopValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRQQQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && !pos.count<KNIGHT>(us)
          && !pos.count<BISHOP>(us)
          && pos.count<QUEEN>(us) >= 3
          && !pos.count<PAWN>(us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1
          && !pos.count<BISHOP>(~us)
          && !pos.count<KNIGHT>(~us)
          && !pos.count<QUEEN>(~us)
          && !pos.count<PAWN>(~us);
  }

  bool is_KRKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg
          && pos.count<ROOK>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KQQQKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<QUEEN>(us) >= 3
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KBQKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + QueenValueMg
          && pos.count<BISHOP>(us) >= 1
          && pos.count<QUEEN >(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KBBKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + BishopValueMg
          && pos.count<BISHOP>(us) == 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KNBQKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KNNQKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + KnightValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 2
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KNQQKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + QueenValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KNBKQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg
          && pos.count<KNIGHT>(us) >= 1
          && pos.count<BISHOP>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == QueenValueMg
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KRKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg
          && pos.count<ROOK>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KNBQKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN >(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KNQQKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + QueenValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KBQQKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + QueenValueMg + QueenValueMg
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KNNQKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + KnightValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 2
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KBBQKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + BishopValueMg + QueenValueMg
          && pos.count<BISHOP>(us) == 2
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KNBKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg
          && pos.count<KNIGHT>(us) >= 1
          && pos.count<BISHOP>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KQQQQKB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= QueenValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<QUEEN>(us) >= 4
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KBQQQQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + QueenValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 4
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KBBQQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + BishopValueMg + QueenValueMg + QueenValueMg
          && pos.count<BISHOP>(us) == 2
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KNQQQQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + QueenValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 4
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KNNQQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + KnightValueMg + QueenValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 2
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KBBNKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg + BishopValueMg
          && pos.count<KNIGHT>(us) >= 1
          && pos.count<BISHOP>(us) == 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KNBBQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg + BishopValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) >= 1
          && pos.count<BISHOP>(us) == 2
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KNNBKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + KnightValueMg + BishopValueMg
          && pos.count<KNIGHT>(us) == 2
          && pos.count<BISHOP>(us) >= 1
          && !pos.count<QUEEN>(us)
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KNNBQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + KnightValueMg + BishopValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 2
          && pos.count<BISHOP>(us) >= 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KNBQQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg + QueenValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KQQQQQKR(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= QueenValueMg + QueenValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<QUEEN>(us) >= 5
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg
          && pos.count<ROOK>(~us) == 1;
  }

  bool is_KRNBQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + BishopValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRNBKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + BishopValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRNQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRBQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + BishopValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRBKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + BishopValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<BISHOP>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRNKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRRKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + RookValueMg
          && pos.count<ROOK>(us) == 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KBQQQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 3
		  && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KNQQQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 3
		  && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KBBQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= BishopValueMg + BishopValueMg + QueenValueMg
          && pos.count<BISHOP>(us) == 2
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KNBQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg + QueenValueMg
          && pos.count<BISHOP>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KNNQQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + KnightValueMg + QueenValueMg + QueenValueMg
          && pos.count<KNIGHT>(us) == 2
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KNNBKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + KnightValueMg + BishopValueMg
          && pos.count<KNIGHT>(us) == 2
          && pos.count<BISHOP>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KNBBKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= KnightValueMg + BishopValueMg + BishopValueMg
          && pos.count<KNIGHT>(us) == 1
          && pos.count<BISHOP>(us) == 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KQQQQQKN(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= QueenValueMg + QueenValueMg + QueenValueMg + QueenValueMg + QueenValueMg
          && pos.count<QUEEN>(us) >= 5
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == KnightValueMg
          && pos.count<KNIGHT>(~us) == 1;
  }

  bool is_KRNQKRQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg + QueenValueMg
          && pos.count<ROOK>(~us) == 1
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KRBQQKRQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + BishopValueMg + QueenValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<BISHOP>(us) == 1
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg + QueenValueMg
          && pos.count<ROOK>(~us) == 1
          && pos.count<QUEEN>(~us) == 1;
  }

  bool is_KRNQQKRB(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + KnightValueMg + QueenValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<KNIGHT>(us) == 1
          && pos.count<QUEEN>(us) >= 2
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == RookValueMg + BishopValueMg
          && pos.count<ROOK>(~us) == 1
          && pos.count<BISHOP>(~us) == 1;
  }

  bool is_KRQKBQ(const Position& pos, Color us) {
    return   pos.non_pawn_material(us) >= RookValueMg + QueenValueMg
          && pos.count<ROOK>(us) == 1
          && pos.count<QUEEN>(us) >= 1
          && !pos.count<PAWN>(~us)
          && pos.non_pawn_material(~us) == BishopValueMg + QueenValueMg
          && pos.count<BISHOP>(~us) == 1
          && pos.count<QUEEN>(~us) == 1; // redghost
  }

  /// imbalance() calculates the imbalance by comparing the piece count of each
  /// piece type for both colors.
  template<Color Us>
  int imbalance(const int pieceCount[][PIECE_TYPE_NB]) {

    constexpr Color Them = (Us == WHITE ? BLACK : WHITE);

    int bonus = 0;

    // Second-degree polynomial material imbalance, by Tord Romstad
    for (int pt1 = NO_PIECE_TYPE; pt1 <= ROOK; ++pt1)
    {
        if (!pieceCount[Us][pt1])
            continue;

        int v = 0;

        for (int pt2 = NO_PIECE_TYPE; pt2 <= pt1; ++pt2)
            v +=  QuadraticOurs[pt1][pt2] * pieceCount[Us][pt2]
                + QuadraticTheirs[pt1][pt2] * pieceCount[Them][pt2];

        bonus += pieceCount[Us][pt1] * v;
    }

    return bonus;
  }

} // namespace

namespace Material {

/// Material::probe() looks up the current position's material configuration in
/// the material hash table. It returns a pointer to the Entry if the position
/// is found. Otherwise a new Entry is computed and stored there, so we don't
/// have to recompute all when the same material configuration occurs again.

Entry* probe(const Position& pos) {

  Key key = pos.material_key();
  Entry* e = pos.this_thread()->materialTable[key];

  if (e->key == key)
      return e;

  std::memset(e, 0, sizeof(Entry));
  e->key = key;
  e->factor[WHITE] = e->factor[BLACK] = (uint8_t)SCALE_FACTOR_NORMAL;

  Value npm_w = pos.non_pawn_material(WHITE);
  Value npm_b = pos.non_pawn_material(BLACK);
  Value npm   = clamp(npm_w + npm_b, EndgameLimit, MidgameLimit);

  // Map total non-pawn material into [PHASE_ENDGAME, PHASE_MIDGAME]
  e->gamePhase = Phase(((npm - EndgameLimit) * PHASE_MIDGAME) / (MidgameLimit - EndgameLimit));

  // Let's look if we have a specialized evaluation function for this particular
  // material configuration. Firstly we look for a fixed configuration one, then
  // for a generic one if the previous search failed.
  if ((e->evaluationFunction = Endgames::probe<Value>(key)) != nullptr)
      return e;

  for (Color c : { WHITE, BLACK })
      if (is_KXK(pos, c))
      {
          e->evaluationFunction = &EvaluateKXK[c];
          return e;
      }

  // Only queens and pawns against bare king
  for (Color c : { WHITE, BLACK })
      if (is_KQsPsK(pos, c))
      {
          e->evaluationFunction = &EvaluateKQsPsK[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KXKRR(pos, c))
      {
          e->evaluationFunction = &EvaluateKXKRR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRXKRR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRXKRR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRRKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRRKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNBQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNBQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNNKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNNKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNBKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNBKR[c];
          return e;
       }

  for (Color c : { WHITE, BLACK })
      if (is_KRNQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRBBKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRBBKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRBQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRBQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRQQQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKRQQQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKRKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KQQQKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKQQQKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBQKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKBQKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBBKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKBBKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBQKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBQKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNNQKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKNNQKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNQQKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKNQQKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBKQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBKQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKRKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBQKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBQKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNQQKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKNQQKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBQQKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKBQQKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNNQKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKNNQKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBBQKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKBBQKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KQQQQKB(pos, c))
      {
          e->evaluationFunction = &EvaluateKQQQQKB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBQQQQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKBQQQQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBBQQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKBBQQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNQQQQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKNQQQQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNNQQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKNNQQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBBNKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKBBNKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBBQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBBQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNNBKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKNNBKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNNBQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKNNBQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBQQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBQQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KQQQQQKR(pos, c))
      {
          e->evaluationFunction = &EvaluateKQQQQQKR[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNBQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNBQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNBKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNBKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRBQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRBQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRBKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRBKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRRKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKRRKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBQQQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKBQQQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNQQQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKNQQQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KBBQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKBBQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNNQQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKNNQQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNNBKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKNNBKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KNBBKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKNBBKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KQQQQQKN(pos, c))
      {
          e->evaluationFunction = &EvaluateKQQQQQKN[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNQKRQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNQKRQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRBQQKRQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKRBQQKRQ[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRNQQKRB(pos, c))
      {
          e->evaluationFunction = &EvaluateKRNQQKRB[c];
          return e;
      }

  for (Color c : { WHITE, BLACK })
      if (is_KRQKBQ(pos, c))
      {
          e->evaluationFunction = &EvaluateKRQKBQ[c];
          return e;
     }

  // OK, we didn't find any special evaluation function for the current material
  // configuration. Is there a suitable specialized scaling function?
  const auto* sf = Endgames::probe<ScaleFactor>(key);

  if (sf)
  {
      e->scalingFunction[sf->strongSide] = sf; // Only strong color assigned
      return e;
  }

  // Evaluate the material imbalance. We use PIECE_TYPE_NONE as a place holder
  // for the bishop pair "extended piece", which allows us to be more flexible
  // in defining bishop pair bonuses.
  const int pieceCount[COLOR_NB][PIECE_TYPE_NB] = {
  { pos.count<QUEEN >(WHITE) > 1, pos.count<PAWN  >(WHITE), pos.count<QUEEN>(WHITE),
    pos.count<BISHOP>(WHITE)    , pos.count<KNIGHT>(WHITE), pos.count<ROOK >(WHITE) },
  { pos.count<QUEEN >(BLACK) > 1, pos.count<PAWN  >(BLACK), pos.count<QUEEN>(BLACK),
    pos.count<BISHOP>(BLACK)    , pos.count<KNIGHT>(BLACK), pos.count<ROOK >(BLACK) } };

  e->value = int16_t((imbalance<WHITE>(pieceCount) - imbalance<BLACK>(pieceCount)) / 16);
  return e;
}

} // namespace Material

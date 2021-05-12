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
#include "pawns.h"
#include "position.h"
#include "thread.h"

namespace {

  #define V Value
  #define S(mg, eg) make_score(mg, eg)

  // Pawn penalties
  constexpr Score Backward      = S( 9, 24);
  constexpr Score Doubled       = S(11, 56);
  constexpr Score Isolated      = S( 5, 15);
  constexpr Score WeakLever     = S( 0, 56);
  constexpr Score WeakUnopposed = S(13, 27);

  // BlockPawn penalties
  constexpr Score BlockedOne   = S(110, 0); //redghost

  // Strength of pawn shelter for our king by [distance from edge][rank].
  // RANK_1 = 0 is used for files where we have no pawn, or pawn is behind our king.
  constexpr Value ShelterStrength[int(FILE_NB) / 2][RANK_NB] = {
    { V( -6), V( 81), V( 93), V( 58), V( 39), V( 18), V(  25) },
    { V(-43), V( 61), V( 35), V(-49), V(-29), V(-11), V( -63) },
    { V(-10), V( 75), V( 23), V( -2), V( 32), V(  3), V( -45) },
    { V(-39), V(-13), V(-29), V(-52), V(-48), V(-67), V(-166) }
  };

  // Danger of enemy pawns moving toward our king by [distance from edge][rank].
  // RANK_1 = 0 is used for files where the enemy has no pawn, or their pawn
  // is behind our king.
  // [0][1-2] accommodate opponent pawn on edge (likely blocked by our king)
  constexpr Value UnblockedStorm[int(FILE_NB) / 2][RANK_NB] = {
    { V( 89), V(-285), V(-185), V(-185), V(57), V( 45), V( 51) },
    { V( 44), V( -18), V( 123), V(  46), V(39), V( -7), V( 23) },
    { V(  4), V(  52), V( 162), V(  27), V( 7), V(-14), V( -2) },
    { V(-10), V( -14), V(  90), V(   4), V( 2), V( -7), V(-16) }
  };

  #undef S
  #undef V

  template<Color Us>
  Score evaluate(const Position& pos, Pawns::Entry* e) {

    constexpr Color     Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Direction Up   = (Us == WHITE ? NORTH : SOUTH);

    Bitboard neighbours, stoppers, doubled, support, phalanx;
    Bitboard lever, leverPush;
    Square s;
    bool opposed, backward, passed;
    Score score = SCORE_ZERO;
    const Square* pl = pos.squares<PAWN>(Us);

    Bitboard ourPawns   = pos.pieces(  Us, PAWN);
    Bitboard theirPawns = pos.pieces(Them, PAWN);

    Bitboard doubleAttackThem = pawn_double_attacks_bb<Them>(theirPawns);

    e->passedPawns[Us] = e->pawnAttacksSpan[Us] = 0;
    e->kingSquares[Us] = SQ_NONE;
    e->pawnAttacks[Us] = pawn_attacks_bb<Us>(ourPawns);

    // Loop through all pawns of the current color and score each pawn
    while ((s = *pl++) != SQ_NONE)
    {
        assert(pos.piece_on(s) == make_piece(Us, PAWN));

        Rank r = relative_rank(Us, s);

        e->pawnAttacksSpan[Us] |= pawn_attack_span(Us, s);

        // Flag the pawn
        opposed    = theirPawns & forward_file_bb(Us, s);
        stoppers   = theirPawns & passed_pawn_span(Us, s);
        lever      = theirPawns & PawnAttacks[Us][s];
        leverPush  = theirPawns & PawnAttacks[Us][s + Up];
        doubled    = ourPawns   & (s - Up);
        neighbours = ourPawns   & adjacent_files_bb(s);
        phalanx    = neighbours & rank_bb(s);
        support    = neighbours & rank_bb(s - Up);

        // A pawn is backward when it is behind all pawns of the same color on
        // the adjacent files and cannot safely advance. Phalanx and isolated
        // pawns will be excluded when the pawn is scored.
        backward =  !(neighbours & forward_ranks_bb(Them, s))
                  && (stoppers & (leverPush | (s + Up)));

        // A pawn is passed if one of the three following conditions is true:
        // (a) there is no stoppers except some levers
        // (b) the only stoppers are the leverPush, but we outnumber them
        // (c) there is only one front stopper which can be levered.
        passed =   !(stoppers ^ lever)
                || (   !(stoppers ^ leverPush)
                    && popcount(phalanx) >= popcount(leverPush))
                || (   stoppers == square_bb(s + Up) && r >= RANK_5
                    && (shift<Up>(support) & ~(theirPawns | doubleAttackThem)));

        // Passed pawns will be properly scored later in evaluation when we have
        // full attack info.
        if (passed)
            e->passedPawns[Us] |= s;

        // Score this pawn
        if (support | phalanx)
        {
            int v = (7 + r * r * r * r / 16) * (phalanx ? 3 : 2) / (opposed ? 2 : 1)
                   + 17 * popcount(support);

            score += make_score(v, v * (r - 2) / 4);
        }

        else if (!neighbours)
            score -= Isolated + WeakUnopposed * int(!opposed);

        else if (backward)
            score -= Backward + WeakUnopposed * int(!opposed);

        if (doubled && !support)
            score -= Doubled;

  //4 pieces redghost
  int countE1 = 0;
  int countE2 = 0;
  int countE4 = 0;
  int countE5 = 0;
  int countE6 = 0;
  int countE7 = 0;
  int countE8 = 0;
  int countE9 = 0;
  int countE10 = 0;
  int countE11 = 0;
  int countE12 = 0;
  int countE13 = 0;
  int countE14 = 0;
  int countE15 = 0;
  int countE16 = 0;
  int countE17 = 0;
  int countE18 = 0;
  int countE19 = 0;
  int countD1 = 0;
  int countD2 = 0;
  int countD4 = 0;
  int countD5 = 0;
  int countD6 = 0;
  int countD7 = 0;
  int countD8 = 0;
  int countD9 = 0;
  int countD10 = 0;
  int countD11 = 0;
  int countD12 = 0;
  int countD13 = 0;
  int countD14 = 0;
  int countD15 = 0;
  int countD16 = 0;
  int countD17 = 0;
  int countD18 = 0;
  int countD19 = 0;
  int countF1 = 0;
  int countF2 = 0;
  int countF3 = 0;
  int countB1 = 0;
  int countB2 = 0;
  int countB3 = 0;
  int countC1 = 0;
  int countC2 = 0;
  int countC3 = 0;
  int countG1 = 0;
  int countG2 = 0;
  int countG3 = 0;
  // 6 pieces US THEM 12 both color redghost
  int countZ1 = 0;
  int countZ2 = 0;
  int countZ3 = 0;
  int countZ4 = 0;
  // 6 pieces US THEM 12 same color redghost
  int countX1 = 0;

  //4 pieces redghost
  countE1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_A5) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_G5))));

  countE2 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4))));

  countE4 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_D3) | relative_square(Us, SQ_F4))));

  countE5 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_G5))));

  countE6 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_H4))));

  countE7 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F3) | relative_square(Us, SQ_G5))));

  countE8 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F4))));

  countE9 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_C3) | relative_square(Us, SQ_F4))));

  countE10 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_A5) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_F4))));

  countE11 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_G5) | relative_square(Us, SQ_H4))));

  countE12 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_G5))));

  countE13 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_G5))));

  countE14 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_G5))));

  countE15 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_D4))));

  countE16 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_A5) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_D4))));

  countE17 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_A5) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_C5))));

  countE18 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_F4))));

  countE19 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_E5)) | relative_square(Us, SQ_A5) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4))));

  countD1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4) | relative_square(Us, SQ_H5))));

  countD2 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_F5))));

  countD4 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E3) | relative_square(Us, SQ_F5))));

  countD5 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4))));

  countD6 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_A4) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4))));

  countD7 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_C3) | relative_square(Us, SQ_E4))));

  countD8 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_F4))));

  countD9 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_F3))));

  countD10 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_H5))));

  countD11 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_A4) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_F5))));

  countD12 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G4))));

  countD13 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F5))));

  countD14 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_F5))));

  countD15 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G4))));

  countD16 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_H5))));

  countD17 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G4) | relative_square(Us, SQ_H5))));

  countD18 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G4))));

  countD19 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_D5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4) | relative_square(Us, SQ_H5))));

  countF1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_F5)) | relative_square(Us, SQ_A4) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4))));

  countF2 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_F5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4) | relative_square(Us, SQ_H5))));

  countF3 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_F5)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4))));

  countB1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_B5)) | relative_square(Us, SQ_A4) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4))));

  countB2 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_B5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_G4) | relative_square(Us, SQ_H5))));

  countB3 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_B5)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_F3) | relative_square(Us, SQ_G4))));

  countC1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_C5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_H4))));

  countC2 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_C5)) | relative_square(Us, SQ_A5) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4))));

  countC3 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_C5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_G5))));

  countG1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_G5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_H4))));

  countG2 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_G5)) | relative_square(Us, SQ_A5) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4))));

  countG3 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_G5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_C3) | relative_square(Us, SQ_D4) | relative_square(Us, SQ_F4))));

  // 6 pieces US THEM 12 both color redghost
  countZ1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_A4)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_G5) | relative_square(Us, SQ_H4)))
                     | (pos.pieces(Them, PAWN) & (square_bb(relative_square(Us, SQ_A5)) | relative_square(Us, SQ_B6) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G6) | relative_square(Us, SQ_H5))));

  countZ2 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_A4)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_G3) | relative_square(Us, SQ_H4)))
                     | (pos.pieces(Them, PAWN) & (square_bb(relative_square(Us, SQ_A5)) | relative_square(Us, SQ_B6) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G6) | relative_square(Us, SQ_H5))));

  countZ3 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_A4)) | relative_square(Us, SQ_B3) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_F4) | relative_square(Us, SQ_G5) | relative_square(Us, SQ_H4)))
                     | (pos.pieces(Them, PAWN) & (square_bb(relative_square(Us, SQ_A5)) | relative_square(Us, SQ_B6) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G6) | relative_square(Us, SQ_H5))));

  countZ4 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_A5)) | relative_square(Us, SQ_B4) | relative_square(Us, SQ_C3) | relative_square(Us, SQ_F3) | relative_square(Us, SQ_G4) | relative_square(Us, SQ_H5)))
                     | (pos.pieces(Them, PAWN) & (square_bb(relative_square(Us, SQ_A6)) | relative_square(Us, SQ_B5) | relative_square(Us, SQ_C6) | relative_square(Us, SQ_F6) | relative_square(Us, SQ_G5) | relative_square(Us, SQ_H6))));

  // 6 pieces US THEM 12 same color redghost
  countX1 = popcount(  (pos.pieces(Us,   PAWN) & (square_bb(relative_square(Us, SQ_B3)) | relative_square(Us, SQ_C4) | relative_square(Us, SQ_E4) | relative_square(Us, SQ_F5) | relative_square(Us, SQ_G4) | relative_square(Us, SQ_H3)))
                     | (pos.pieces(Them, PAWN) & (square_bb(relative_square(Us, SQ_B4)) | relative_square(Us, SQ_C5) | relative_square(Us, SQ_D6) | relative_square(Us, SQ_E5) | relative_square(Us, SQ_G5) | relative_square(Us, SQ_H4))));

  //4 pieces redghost
  if ((countE1 >= 5) | (countE2 >= 4)| (countE4 >= 4) | (countE5 >= 4) | (countE6 >= 5)| (countE7 >= 4) | (countE8 >= 4) | (countE9 >= 4) | (countE10 >= 4) | (countE11 >= 4) | (countE12 >= 4) | (countE13 >= 4)
       | (countE14 >= 4) | (countE15 >= 4) | (countE16 >= 4) | (countE17 >= 4) | (countE18 >= 4) | (countE19 >= 5) | (countC1 >= 5) | (countC2 >= 5) | (countC3 >= 5) | (countG1 >= 5) | (countG2 >= 5) | (countG3 >= 5))
      score -= BlockedOne;
  if ((countD1 >= 5) | (countD2 >= 4)| (countD4 >= 4) | (countD5 >= 4) | (countD6 >= 5)| (countD7 >= 4) | (countD8 >= 4) | (countD9 >= 4) | (countD10 >= 4) | (countD11 >= 4) | (countD12 >= 4) | (countD13 >= 4)
       | (countD14 >= 4) | (countD15 >= 4) | (countD16 >= 4) | (countD17 >= 4) | (countD18 >= 4) | (countD19 >= 5) | (countF1 >= 5) | (countF2 >= 5) | (countF3 >= 5) | (countB1 >= 5) | (countB2 >= 5) | (countB3 >= 5))
      score -= BlockedOne;
  // 6 pieces US THEM 12 both color redghost
  if (countZ1 >= 12)
      score -= BlockedOne;
  if (countZ2 >= 12)
      score -= BlockedOne;
  if (countZ3 >= 12)
      score -= BlockedOne;
  if (countZ4 >= 12)
      score -= BlockedOne;
  // 6 pieces US THEM 12 same color redghost
  if (countX1 >= 12)
      score -= BlockedOne;
    }

    // Penalize our unsupported pawns attacked twice by enemy pawns
    score -= WeakLever * popcount(  ourPawns
                                  & doubleAttackThem
                                  & ~e->pawnAttacks[Us]);

    return score;
  }

} // namespace

namespace Pawns {

/// Pawns::probe() looks up the current position's pawns configuration in
/// the pawns hash table. It returns a pointer to the Entry if the position
/// is found. Otherwise a new Entry is computed and stored there, so we don't
/// have to recompute all when the same pawns configuration occurs again.

Entry* probe(const Position& pos) {

  Key key = pos.pawn_key();
  Entry* e = pos.this_thread()->pawnsTable[key];

  if (e->key == key)
      return e;

  e->key = key;
  e->scores[WHITE] = evaluate<WHITE>(pos, e);
  e->scores[BLACK] = evaluate<BLACK>(pos, e);

  return e;
}


/// Entry::evaluate_shelter() calculates the shelter bonus and the storm
/// penalty for a king, looking at the king file and the two closest files.

template<Color Us>
void Entry::evaluate_shelter(const Position& pos, Square ksq, Score& shelter) {

  constexpr Color Them = (Us == WHITE ? BLACK : WHITE);

  Bitboard b = pos.pieces(PAWN) & ~forward_ranks_bb(Them, ksq);
  Bitboard ourPawns = b & pos.pieces(Us);
  Bitboard theirPawns = b & pos.pieces(Them);

  Score bonus = make_score(5, 5);

  File center = clamp(file_of(ksq), FILE_B, FILE_G);
  for (File f = File(center - 1); f <= File(center + 1); ++f)
  {
      b = ourPawns & file_bb(f);
      Rank ourRank = b ? relative_rank(Us, frontmost_sq(Them, b)) : RANK_1;

      b = theirPawns & file_bb(f);
      Rank theirRank = b ? relative_rank(Us, frontmost_sq(Them, b)) : RANK_1;

      int d = std::min(f, ~f);
      bonus += make_score(ShelterStrength[d][ourRank], 0);

      if (ourRank && (ourRank == theirRank - 1))
          bonus -= make_score(41 * (theirRank == RANK_4), 41 * (theirRank == RANK_4));
      else
          bonus -= make_score(UnblockedStorm[d][theirRank], 0);
  }

  if (mg_value(bonus) > mg_value(shelter))
      shelter = bonus;
}


/// Entry::do_king_safety() calculates a bonus for king safety. It is called only
/// when king square changes, which is about 20% of total king_safety() calls.

template<Color Us>
Score Entry::do_king_safety(const Position& pos) {

  Square ksq = pos.square<KING>(Us);
  kingSquares[Us] = ksq;

  Bitboard pawns = pos.pieces(Us, PAWN);
  int minPawnDist = pawns ? 8 : 0;

  if (pawns & PseudoAttacks[KING][ksq])
      minPawnDist = 1;

  else while (pawns)
      minPawnDist = std::min(minPawnDist, distance(ksq, pop_lsb(&pawns)));

  Score shelter = make_score(-VALUE_INFINITE, VALUE_ZERO);
  evaluate_shelter<Us>(pos, ksq, shelter);

  return shelter - make_score(VALUE_ZERO, 16 * minPawnDist);
}

// Explicit template instantiation
template Score Entry::do_king_safety<WHITE>(const Position& pos);
template Score Entry::do_king_safety<BLACK>(const Position& pos);

} // namespace Pawns

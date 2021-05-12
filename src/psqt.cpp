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

#include "types.h"

Value PieceValue[PHASE_NB][PIECE_NB] = {
  { VALUE_ZERO, PawnValueMg, QueenValueMg, BishopValueMg, KnightValueMg, RookValueMg },
  { VALUE_ZERO, PawnValueEg, QueenValueEg, BishopValueEg, KnightValueEg, RookValueEg }
};

namespace PSQT {

#define S(mg, eg) make_score(mg, eg)

// Bonus[PieceType][Square / 2] contains Piece-Square scores. For each piece
// type on a given square a (middlegame, endgame) score pair is assigned. Table
// is defined for files A..D and white side: redghost it is symmetric for black side and
// second half of the files.
constexpr Score Bonus[][RANK_NB][int(FILE_NB) / 2] = {
  { },
  { // Pawn
   { S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0) },
   { S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0) },
   { S(-18,-4), S(-2,-5), S( 9, 5), S(14, 4) },
   { S(-17, 3), S(10, 3), S(15,-8), S(20,-3) },
   { S( -6, 8), S( 1, 9), S( 8, 7), S( 9,-6) }
  },
  { // Queen
   { S(-175, -96), S(-92,-65), S(-74,-49), S(-73,-21) },
   { S( -77, -67), S(-41,-54), S(-27,-18), S(-15,  8) },
   { S( -61, -40), S(-22,-27), S(151, -8), S(257, 29) },
   { S(  -1, -35), S( 68, -2), S(257, 13), S(273, 28) },
   { S( -14, -45), S( 73,-16), S(261,  9), S(287, 39) },
   { S(  -9, -51), S( 82,-44), S(267,-16), S(290, 17) },
   { S( -67, -69), S(-27,-50), S(  4,-51), S( 37, 12) },
   { S(-201,-100), S(-83,-88), S(-56,-56), S(-26,-17) }
  },
  { // Bishop
   { S(-175, -96), S(-92,-65), S(-74,-49), S(-73,-21) },
   { S( -37, -67), S(-21,-54), S( -1,-18), S( -1,  8) },
   { S(  -3, -40), S( 65,-27), S(151, -8), S(157, 29) },
   { S(  45, -35), S( 68, -2), S(170, 13), S(179, 28) },
   { S(  -2, -45), S( 73,-16), S(174,  9), S(181, 39) },
   { S(  -1, -51), S( 82,-44), S(183,-16), S(188, 17) },
   { S( -67, -69), S(-27,-50), S(  4,-51), S( 37, 12) },
   { S(-201,-100), S(-83,-88), S(-56,-56), S(-26,-17) }
  },
  { // Knight
   { S(-175, -96), S(-92,-65), S(-74,-49), S(-73,-21) },
   { S( -77, -67), S(-41,-54), S(-27,-18), S(  0,  8) },
   { S( -61, -40), S( -2,-27), S(  0, -8), S( 12, 29) },
   { S( -35, -35), S(  8, -2), S( 40, 13), S( 49, 28) },
   { S( -34, -45), S( 13,-16), S( 44,  9), S( 51, 39) },
   { S(  -9, -51), S( 22,-44), S( 58,-16), S( 53, 17) },
   { S( -67, -69), S(-27,-50), S(  4,-51), S( 37, 12) },
   { S(-201,-100), S(-83,-88), S(-56,-56), S(-26,-17) }
  },
  { // Rook
   { S(-31, -9), S(-20,-13), S(-14,-10), S(-5, -9) },
   { S(-21,-12), S(-13, -9), S( -8, -1), S( 6, -2) },
   { S(-25,  6), S(-11, -8), S( -1, -2), S( 3, -6) },
   { S(-13, -6), S( -5,  1), S( -4, -9), S(-6,  7) },
   { S(-27, -5), S(-15,  8), S( -4,  7), S( 3, -6) },
   { S(-22,  6), S( -2,  1), S(  6, -7), S(12, 10) },
   { S( -2,  4), S( 12,  5), S( 16, 20), S(18, -5) },
   { S(-17, 18), S(-19,  0), S( -1, 19), S( 9, 13) }
  },
  { // King
   { S( 0,  1), S(  0, 45), S( 32, 85), S(285, 76) },
   { S(91, 53), S(158,100), S(120,133), S(152,135) },
   { S(99, 88), S(126,130), S( 84,169), S( 60,175) },
   { S(84,103), S( 95,156), S( 68,172), S( 54,172) },
   { S(72, 96), S( 88,166), S( 56,199), S( 34,199) },
   { S(61, 92), S( 79,172), S( 42,184), S( 18,191) },
   { S(43, 47), S( 60,121), S( 32,116), S( 12,131) },
   { S( 0, 11), S( 44, 59), S( 24, 73), S( 10, 78) }
  }
};

#undef S

Score psq[PIECE_NB][SQUARE_NB];

// init() initializes piece-square tables: the white halves of the tables are
// copied from Bonus[] adding the piece value, then the black halves of the
// tables are initialized by flipping and changing the sign of the white scores.
void init() {

  for (Piece pc = W_PAWN; pc <= W_KING; ++pc)
  {
      PieceValue[MG][~pc] = PieceValue[MG][pc];
      PieceValue[EG][~pc] = PieceValue[EG][pc];

      Score score = make_score(PieceValue[MG][pc], PieceValue[EG][pc]);

      for (Square s = SQ_A1; s <= SQ_H8; ++s)
      {
          File f = std::min(file_of(s), ~file_of(s));
          psq[ pc][ s] = score + Bonus[pc][rank_of(s)][f];
          psq[~pc][~s] = -psq[pc][s];
      }
  }
}

} // namespace PSQT

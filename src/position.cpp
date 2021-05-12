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
#include <cstddef> // For offsetof()
#include <cstring> // For std::memset, std::memcmp
#include <iomanip>
#include <sstream>

#include "bitboard.h"
#include "misc.h"
#include "movegen.h"
#include "position.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

using std::string;

namespace Zobrist {

  Key psq[PIECE_NB][SQUARE_NB];
  Key side, noPawns;
}

namespace {

const string PieceToChar(" PMSNRK  pmsnrk");

constexpr Piece Pieces[] = { W_PAWN, W_QUEEN, W_BISHOP, W_KNIGHT, W_ROOK, W_KING,
                             B_PAWN, B_QUEEN, B_BISHOP, B_KNIGHT, B_ROOK, B_KING };

// min_attacker() is a helper function used by see_ge() to locate the least
// valuable attacker for the side to move, remove the attacker we just found
// from the bitboards and scan for new X-ray attacks behind it.

template<PieceType Pt>
PieceType min_attacker(const Bitboard* byTypeBB, Square to, Bitboard stmAttackers,
                       Bitboard& occupied, Bitboard& attackers) {

  Bitboard b = stmAttackers & byTypeBB[Pt];
  if (!b)
      return min_attacker<PieceType(Pt + 1)>(byTypeBB, to, stmAttackers, occupied, attackers);

  occupied ^= lsb(b); // Remove the attacker from occupied

  // Add any X-ray attack behind the just removed piece. For instance with
  // rooks in a8 and a7 attacking a1, after removing a7 we add rook in a8.
  // Note that new added attackers can be of any color.
  if (Pt == BISHOP || Pt == ROOK)
      attackers |= attacks_bb<ROOK>(to, occupied) & byTypeBB[ROOK];

  // X-ray may add already processed pieces because byTypeBB[] is constant: in
  // the rook example, now attackers contains _again_ rook in a7, so remove it.
  attackers &= occupied;
  return Pt;
}

template<>
PieceType min_attacker<KING>(const Bitboard*, Square, Bitboard, Bitboard&, Bitboard&) {
  return KING; // No need to update bitboards: it is the last cycle
}

} // namespace


/// operator<<(Position) returns an ASCII representation of the position

std::ostream& operator<<(std::ostream& os, const Position& pos) {

  os << "\n +---+---+---+---+---+---+---+---+\n";

  for (Rank r = RANK_8; r >= RANK_1; --r)
  {
      for (File f = FILE_A; f <= FILE_H; ++f)
          os << " | " << PieceToChar[pos.piece_on(make_square(f, r))];

      os << " |\n +---+---+---+---+---+---+---+---+\n";
  }

  os << "\nFen: " << pos.fen() << "\nKey: " << std::hex << std::uppercase
     << std::setfill('0') << std::setw(16) << pos.key()
     << std::setfill(' ') << std::dec << "\nCheckers: ";

  for (Bitboard b = pos.checkers(); b; )
      os << UCI::square(pop_lsb(&b)) << " ";

  if (int(Tablebases::MaxCardinality) >= popcount(pos.pieces()))
  {
      StateInfo st;
      Position p;
      p.set(pos.fen(), pos.is_chess960(), &st, pos.this_thread());
      Tablebases::ProbeState s1, s2;
      Tablebases::WDLScore wdl = Tablebases::probe_wdl(p, &s1);
      int dtz = Tablebases::probe_dtz(p, &s2);
      os << "\nTablebases WDL: " << std::setw(4) << wdl << " (" << s1 << ")"
         << "\nTablebases DTZ: " << std::setw(4) << dtz << " (" << s2 << ")";
  }

  return os;
}


// Marcel van Kervinck's cuckoo algorithm for fast detection of "upcoming repetition"
// situations. Description of the algorithm in the following paper:
// https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// First and second hash functions for indexing the cuckoo tables
inline int H1(Key h) { return h & 0x1fff; }
inline int H2(Key h) { return (h >> 16) & 0x1fff; }

// Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
Key cuckoo[8192];
Move cuckooMove[8192];


/// Position::init() initializes at startup the various arrays used to compute
/// hash keys.

void Position::init() {

  PRNG rng(1070372);

  for (Piece pc : Pieces)
      for (Square s = SQ_A1; s <= SQ_H8; ++s)
          Zobrist::psq[pc][s] = rng.rand<Key>();

  Zobrist::side = rng.rand<Key>();
  Zobrist::noPawns = rng.rand<Key>();

  // Prepare the cuckoo tables
  std::memset(cuckoo, 0, sizeof(cuckoo));
  std::memset(cuckooMove, 0, sizeof(cuckooMove));
  int count = 0;
  for (Piece pc : Pieces)
      for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
          for (Square s2 = Square(s1 + 1); s2 <= SQ_H8; ++s2)
              if (PseudoAttacks[type_of(pc)][s1] & s2)
              {
                  Move move = make_move(s1, s2);
                  Key key = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side;
                  int i = H1(key);
                  while (true)
                  {
                      std::swap(cuckoo[i], key);
                      std::swap(cuckooMove[i], move);
                      if (move == MOVE_NONE) // Arrived at empty slot?
                          break;
                      i = (i == H1(key)) ? H2(key) : H1(key); // Push victim to alternative slot
                  }
                  count++;
             }
  assert(count == 3668);
}


/// Position::set() initializes the position object with the given FEN string.
/// This function is not very robust - make sure that input FENs are correct,
/// this is assumed to be the responsibility of the GUI.

Position& Position::set(const string& fenStr, bool isChess960, StateInfo* si, Thread* th) {
/*
   A FEN string defines a particular position using only the ASCII character set.

   A FEN string contains six fields separated by a space. The fields are:

   1) Piece placement (from white's perspective). Each rank is described, starting
      with rank 8 and ending with rank 1. Within each rank, the contents of each
      square are described from file A through file H. Following the Standard
      Algebraic Notation (SAN), each piece is identified by a single letter taken
      from the standard English names. White pieces are designated using upper-case
      letters ("PNBRQK") whilst Black uses lowercase ("pnbrqk"). Blank squares are
      noted using digits 1 through 8 (the number of blank squares), and "/"
      separates ranks.

   2) Active color. "w" means white moves next, "b" means black.

   3) -

   4) -

   5) Halfmove clock. This is the number of halfmoves since the last pawn advance
      or capture. This is used to determine if a draw can be claimed under the
      fifty-move rule.

   6) Fullmove number. The number of the full move. It starts at 1, and is
      incremented after Black's move.
*/

  unsigned char token;
  size_t idx;
  Square sq = SQ_A8;
  std::istringstream ss(fenStr);

  std::memset(this, 0, sizeof(Position));
  std::memset(si, 0, sizeof(StateInfo));
  std::fill_n(&pieceList[0][0], sizeof(pieceList) / sizeof(Square), SQ_NONE);
  st = si;

  ss >> std::noskipws;

  // 1. Piece placement
  while ((ss >> token) && !isspace(token))
  {
      if (isdigit(token))
          sq += (token - '0') * EAST; // Advance the given number of files

      else if (token == '/')
          sq += 2 * SOUTH;

      else if ((idx = PieceToChar.find(token)) != string::npos)
      {
          put_piece(Piece(idx), sq);
          ++sq;
      }
  }

  // 2. Active color
  ss >> token;
  sideToMove = (token == 'w' ? WHITE : BLACK);
  ss >> token;

  // 3-4. Skip castling and en passant flags if present
  if (!isdigit(ss.peek()))
  {
      while ((ss >> token) && !isspace(token)) {}
      while ((ss >> token) && !isspace(token)) {}
  }

  // 5-6. Halfmove clock and fullmove number
  ss >> std::skipws >> st->rule50 >> gamePly;

  // Convert from fullmove starting from 1 to gamePly starting from 0,
  // handle also common incorrect FEN with fullmove = 0.
  gamePly = std::max(2 * (gamePly - 1), 0) + (sideToMove == BLACK);

  chess960 = isChess960;
  thisThread = th;
  set_state(st);

  assert(pos_is_ok());

  return *this;
}


/// Position::set_check_info() sets king attacks to detect if a move gives check

void Position::set_check_info(StateInfo* si) const {

  si->blockersForKing[WHITE] = slider_blockers(pieces(BLACK), square<KING>(WHITE), si->pinners[BLACK]);
  si->blockersForKing[BLACK] = slider_blockers(pieces(WHITE), square<KING>(BLACK), si->pinners[WHITE]);

  Square ksq = square<KING>(~sideToMove);

  si->checkSquares[PAWN]   = attacks_from<PAWN>(ksq, ~sideToMove);
  si->checkSquares[QUEEN]  = attacks_from<QUEEN>(ksq);
  si->checkSquares[BISHOP] = attacks_from<BISHOP>(ksq, ~sideToMove);
  si->checkSquares[KNIGHT] = attacks_from<KNIGHT>(ksq);
  si->checkSquares[ROOK]   = attacks_from<ROOK>(ksq);
  si->checkSquares[KING]   = 0;
}


/// Position::set_state() computes the hash keys of the position, and other
/// data that once computed is updated incrementally as moves are made.
/// The function is only used when a new position is set up, and to verify
/// the correctness of the StateInfo data when running in debug mode.

void Position::set_state(StateInfo* si) const {

  si->key = si->materialKey = 0;
  si->pawnKey = Zobrist::noPawns;
  si->nonPawnMaterial[WHITE] = si->nonPawnMaterial[BLACK] = VALUE_ZERO;
  si->checkersBB = attackers_to(square<KING>(sideToMove)) & pieces(~sideToMove);

  set_check_info(si);

  for (Bitboard b = pieces(); b; )
  {
      Square s = pop_lsb(&b);
      Piece pc = piece_on(s);
      si->key ^= Zobrist::psq[pc][s];

      if (type_of(pc) == PAWN)
          si->pawnKey ^= Zobrist::psq[pc][s];

      else if (type_of(pc) != KING)
          si->nonPawnMaterial[color_of(pc)] += PieceValue[MG][pc];
  }

  if (sideToMove == BLACK)
      si->key ^= Zobrist::side;

  for (Piece pc : Pieces)
      for (int cnt = 0; cnt < pieceCount[pc]; ++cnt)
          si->materialKey ^= Zobrist::psq[pc][cnt];
}


/// Position::set() is an overload to initialize the position object with
/// the given endgame code string like "KBPKN". It is mainly a helper to
/// get the material key out of an endgame code.

Position& Position::set(const string& code, Color c, StateInfo* si) {

  assert(code.length() > 0 && code.length() < 8);
  assert(code[0] == 'K');

  string sides[] = { code.substr(code.find('K', 1)),      // Weak
                     code.substr(0, code.find('K', 1)) }; // Strong

  std::transform(sides[c].begin(), sides[c].end(), sides[c].begin(), tolower);

  string fenStr = "8/" + sides[0] + char(8 - sides[0].length() + '0') + "/8/8/8/8/"
                       + sides[1] + char(8 - sides[1].length() + '0') + "/8 w 0 10";

  return set(fenStr, false, si, nullptr);
}


/// Position::fen() returns a FEN representation of the position. In case of
/// Chess960 the Shredder-FEN notation is used. This is mainly a debugging function.

const string Position::fen() const {

  int emptyCnt;
  std::ostringstream ss;

  for (Rank r = RANK_8; r >= RANK_1; --r)
  {
      for (File f = FILE_A; f <= FILE_H; ++f)
      {
          for (emptyCnt = 0; f <= FILE_H && empty(make_square(f, r)); ++f)
              ++emptyCnt;

          if (emptyCnt)
              ss << emptyCnt;

          if (f <= FILE_H)
              ss << PieceToChar[piece_on(make_square(f, r))];
      }

      if (r > RANK_1)
          ss << '/';
  }

  ss << (sideToMove == WHITE ? " w " : " b ");

  ss << st->rule50 << " " << 1 + (gamePly - (sideToMove == BLACK)) / 2;

  return ss.str();
}


/// Position::slider_blockers() returns a bitboard of all the pieces (both colors)
/// that are blocking attacks on the square 's' from 'sliders'. A piece blocks a
/// slider if removing that piece from the board would result in a position where
/// square 's' is attacked. For example, a king-attack blocking piece can be either
/// a pinned or a discovered check piece, according if its color is the opposite
/// or the same of the color of the slider.

Bitboard Position::slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const {

  Bitboard blockers = 0;
  pinners = 0;

  // Snipers are sliders that attack 's' when a piece and other snipers are removed
  Bitboard snipers = PseudoAttacks[ROOK][s] & pieces(ROOK) & sliders;
  Bitboard occupancy = pieces() ^ snipers;

  while (snipers)
  {
    Square sniperSq = pop_lsb(&snipers);
    Bitboard b = between_bb(s, sniperSq) & occupancy;

    if (b && !more_than_one(b))
    {
        blockers |= b;
        if (b & pieces(color_of(piece_on(s))))
            pinners |= sniperSq;
    }
  }
  return blockers;
}


/// Position::attackers_to() computes a bitboard of all pieces which attack a
/// given square. Slider attacks use the occupied bitboard to indicate occupancy.

Bitboard Position::attackers_to(Square s, Bitboard occupied) const {

  return  (attacks_from<PAWN>(s, BLACK)    & pieces(WHITE, PAWN))
        | (attacks_from<PAWN>(s, WHITE)    & pieces(BLACK, PAWN))
        | (attacks_from<QUEEN>(s)          & pieces(QUEEN))
        | (attacks_from<BISHOP>(s, BLACK)  & pieces(WHITE, BISHOP))
        | (attacks_from<BISHOP>(s, WHITE)  & pieces(BLACK, BISHOP))
        | (attacks_from<KNIGHT>(s)         & pieces(KNIGHT))
        | (attacks_bb<  ROOK>(s, occupied) & pieces(ROOK))
        | (attacks_from<KING>(s)           & pieces(KING));
}


/// Position::legal() tests whether a pseudo-legal move is legal

bool Position::legal(Move m) const {

  assert(is_ok(m));

  Color us = sideToMove;
  Square from = from_sq(m);

  assert(color_of(moved_piece(m)) == us);
  assert(piece_on(square<KING>(us)) == make_piece(us, KING));

  // If the moving piece is a king, check whether the destination square is
  // attacked by the opponent.
  if (type_of(piece_on(from)) == KING)
      return !(attackers_to(to_sq(m)) & pieces(~us));

  // A non-king move is legal if and only if it is not pinned or it
  // is moving along the ray towards or away from the king.
  return   !(blockers_for_king(us) & from)
        ||  aligned(from, to_sq(m), square<KING>(us));
}


/// Position::pseudo_legal() takes a random move and tests whether the move is
/// pseudo legal. It is used to validate moves from TT that can be corrupted
/// due to SMP concurrent access or hash position key aliasing.

bool Position::pseudo_legal(const Move m) const {

  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = moved_piece(m);

  const Bitboard TRank6BB = (us == WHITE ? Rank6BB : Rank3BB);

  // Use a slower but simpler function for uncommon cases
  if (type_of(m) != NORMAL)
      return MoveList<LEGAL>(*this).contains(m);

  // Is not a promotion, so promotion piece must be empty
  if (promotion_type(m) - QUEEN != NO_PIECE_TYPE)
      return false;

  // If the 'from' square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (pc == NO_PIECE || color_of(pc) != us)
      return false;

  // The destination square cannot be occupied by a friendly piece
  if (pieces(us) & to)
      return false;

  // Handle the special case of a pawn move
  if (type_of(pc) == PAWN)
  {
      // We have already handled promotion moves, so destination
      // cannot be on the 8th/1st rank.
      if (type_of(pc) == PAWN && (TRank6BB & to)) //redghost
          return false;

      if (   !(attacks_from<PAWN>(from, us) & pieces(~us) & to) // Not a capture
          && !((from + pawn_push(us) == to) && empty(to)))      // Not a single push

          return false;
  }
  else if (type_of(pc) == BISHOP ? !(attacks_from<BISHOP>(from, color_of(pc)) & to)
                                 : !(attacks_from(type_of(pc), from) & to))
      return false;

  // Evasions generator already takes care to avoid some kind of illegal moves
  // and legal() relies on this. We therefore have to take care that the same
  // kind of moves are filtered out here.
  if (checkers())
  {
      if (type_of(pc) != KING)
      {
          // Double check? In this case a king move is required
          if (more_than_one(checkers()))
              return false;

          // Our move must be a blocking evasion or a capture of the checking piece
          if (!((between_bb(lsb(checkers()), square<KING>(us)) | checkers()) & to))
              return false;
      }
      // In case of king moves under check we have to remove king so as to catch
      // invalid moves like b1a1 when opposite queen is on c1.
      else if (attackers_to(to, pieces() ^ from) & pieces(~us))
          return false;
  }

  return true;
}


/// Position::gives_check() tests whether a pseudo-legal move gives a check

bool Position::gives_check(Move m) const {

  assert(is_ok(m));
  assert(color_of(moved_piece(m)) == sideToMove);

  Square from = from_sq(m);
  Square to = to_sq(m);

  // Is there a direct check?
  if (st->checkSquares[type_of(piece_on(from))] & to)
      return true;

  // Is there a discovered check?
  if (   (st->blockersForKing[~sideToMove] & from)
      && !aligned(from, to, square<KING>(~sideToMove)))
      return true;

  switch (type_of(m))
  {
  case NORMAL:
      return false;

  case PROMOTION:
      return attacks_bb(promotion_type(m), to, pieces() ^ from) & square<KING>(~sideToMove);

  default:
      assert(false);
      return false;
  }
}


/// Position::do_move() makes a move, and saves all information necessary
/// to a StateInfo object. The move is assumed to be legal. Pseudo-legal
/// moves should be filtered out before this function is called.

void Position::do_move(Move m, StateInfo& newSt, bool givesCheck) {

  assert(is_ok(m));
  assert(&newSt != st);

  thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
  Key k = st->key ^ Zobrist::side;

  // Copy some fields of the old state to our new StateInfo object except the
  // ones which are going to be recalculated from scratch anyway and then switch
  // our state pointer to point to the new (ready to be updated) state.
  std::memcpy(&newSt, st, offsetof(StateInfo, key));
  newSt.previous = st;
  st = &newSt;

  // Increment ply counters. In particular, rule50 will be reset to zero later on
  // in case of a capture or a pawn move.
  ++gamePly;
  ++st->rule50;
  ++st->pliesFromNull;

  Color us = sideToMove;
  Color them = ~us;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_on(from);
  Piece captured = piece_on(to);

  assert(color_of(pc) == us);
  assert(captured == NO_PIECE || color_of(captured) == them);
  assert(type_of(captured) != KING);

  if (captured)
  {
      Square capsq = to;

      // If the captured piece is a pawn, update pawn hash key, otherwise
      // update non-pawn material.
      if (type_of(captured) == PAWN)
          st->pawnKey ^= Zobrist::psq[captured][capsq];
      else
          st->nonPawnMaterial[them] -= PieceValue[MG][captured];

      // Update board and piece lists
      remove_piece(captured, capsq);

      // Update material hash key and prefetch access to materialTable
      k ^= Zobrist::psq[captured][capsq];
      st->materialKey ^= Zobrist::psq[captured][pieceCount[captured]];
      prefetch(thisThread->materialTable[st->materialKey]);

      // Reset rule 50 counter unless we are in a pawnless endgame
      if (count<PAWN>() || (type_of(captured) == PAWN && count<ALL_PIECES>(color_of(captured)) > 1))
          st->rule50 = 0;
  }

  // Update hash key
  k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

  move_piece(pc, from, to);

  // If the moving piece is a pawn do some special extra work
  if (type_of(pc) == PAWN)
  {
      if (type_of(m) == PROMOTION)
      {
          Piece promotion = make_piece(us, promotion_type(m));

          assert(relative_rank(us, to) == RANK_6);
          assert(type_of(promotion) == QUEEN);

          remove_piece(pc, to);
          put_piece(promotion, to);

          // Update hash keys
          k ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promotion][to];
          st->pawnKey ^= Zobrist::psq[pc][to];
          st->materialKey ^=  Zobrist::psq[promotion][pieceCount[promotion]-1]
                            ^ Zobrist::psq[pc][pieceCount[pc]];

          // Update material
          st->nonPawnMaterial[us] += PieceValue[MG][promotion];
      }

      // Update pawn hash key and prefetch access to pawnsTable
      st->pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

      // Reset rule 50 draw counter
      st->rule50 = 0;
  }

  // Set capture piece
  st->capturedPiece = captured;

  // Update the key with the final value
  st->key = k;

  // Calculate checkers bitboard (if move gives check)
  st->checkersBB = givesCheck ? attackers_to(square<KING>(them)) & pieces(us) : 0;

  sideToMove = ~sideToMove;

  // Update king attacks used for fast check detection
  set_check_info(st);

  // Calculate the repetition info. It is the ply distance from the previous
  // occurrence of the same position, negative in the 3-fold case, or zero
  // if the position was not repeated.
  st->repetition = 0;
  int end = std::min(st->rule50, st->pliesFromNull);
  if (end >= 4)
  {
      StateInfo* stp = st->previous->previous;
      for (int i=4; i <= end; i += 2)
      {
          stp = stp->previous->previous;
          if (stp->key == st->key)
          {
              st->repetition = stp->repetition ? -i : i;
              break;
          }
      }
  }

  assert(pos_is_ok());
}


/// Position::undo_move() unmakes a move. When it returns, the position should
/// be restored to exactly the same state as before the move was made.

void Position::undo_move(Move m) {

  assert(is_ok(m));

  sideToMove = ~sideToMove;

  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_on(to);

  assert(empty(from));
  assert(type_of(st->capturedPiece) != KING);

  if (type_of(m) == PROMOTION)
  {
      assert(relative_rank(us, to) == RANK_6);
      assert(type_of(pc) == promotion_type(m));
      assert(type_of(pc) == QUEEN);

      remove_piece(pc, to);
      pc = make_piece(us, PAWN);
      put_piece(pc, to);
  }

  move_piece(pc, to, from); // Put the piece back at the source square

  if (st->capturedPiece)
  {
      Square capsq = to;
      put_piece(st->capturedPiece, capsq); // Restore the captured piece
  }

  // Finally point our state pointer back to the previous state
  st = st->previous;
  --gamePly;

  assert(pos_is_ok());
}


/// Position::do(undo)_null_move() is used to do(undo) a "null move": It flips
/// the side to move without executing any move on the board.

void Position::do_null_move(StateInfo& newSt) {

  assert(!checkers());
  assert(&newSt != st);

  std::memcpy(&newSt, st, sizeof(StateInfo));
  newSt.previous = st;
  st = &newSt;

  st->key ^= Zobrist::side;
  prefetch(TT.first_entry(st->key));

  ++st->rule50;
  st->pliesFromNull = 0;

  sideToMove = ~sideToMove;

  set_check_info(st);

  st->repetition = 0;

  assert(pos_is_ok());
}

void Position::undo_null_move() {

  assert(!checkers());

  st = st->previous;
  sideToMove = ~sideToMove;
}


/// Position::key_after() computes the new hash key after the given move. Needed
/// for speculative prefetch.

Key Position::key_after(Move m) const {

  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_on(from);
  Piece captured = piece_on(to);
  Key k = st->key ^ Zobrist::side;

  if (captured)
      k ^= Zobrist::psq[captured][to];

  return k ^ Zobrist::psq[pc][to] ^ Zobrist::psq[pc][from];
}


/// Position::see_ge (Static Exchange Evaluation Greater or Equal) tests if the
/// SEE value of move is greater or equal to the given threshold. We'll use an
/// algorithm similar to alpha-beta pruning with a null window.

bool Position::see_ge(Move m, Value threshold) const {

  assert(is_ok(m));

  // Only deal with normal moves, assume others pass a simple see
  if (type_of(m) != NORMAL)
      return VALUE_ZERO >= threshold;

  Bitboard stmAttackers;
  Square from = from_sq(m), to = to_sq(m);
  PieceType nextVictim = type_of(piece_on(from));
  Color us = color_of(piece_on(from));
  Color stm = ~us; // First consider opponent's move
  Value balance;   // Values of the pieces taken by us minus opponent's ones

  // The opponent may be able to recapture so this is the best result
  // we can hope for.
  balance = PieceValue[MG][piece_on(to)] - threshold;

  if (balance < VALUE_ZERO)
      return false;

  // Now assume the worst possible result: that the opponent can
  // capture our piece for free.
  balance -= PieceValue[MG][nextVictim];

  // If it is enough (like in PxQ) then return immediately. Note that
  // in case nextVictim == KING we always return here, this is ok
  // if the given move is legal.
  if (balance >= VALUE_ZERO)
      return true;

  // Find all attackers to the destination square, with the moving piece
  // removed, but possibly an X-ray attacker added behind it.
  Bitboard occupied = pieces() ^ from ^ to;
  Bitboard attackers = attackers_to(to, occupied) & occupied;

  while (true)
  {
      stmAttackers = attackers & pieces(stm);

      // Don't allow pinned pieces to attack (except the king) as long as
      // any pinners are on their original square.
      if (st->pinners[~stm] & occupied)
          stmAttackers &= ~st->blockersForKing[stm];

      // If stm has no more attackers then give up: stm loses
      if (!stmAttackers)
          break;

      // Locate and remove the next least valuable attacker, and add to
      // the bitboard 'attackers' the possibly X-ray attackers behind it.
      nextVictim = min_attacker<PAWN>(byTypeBB, to, stmAttackers, occupied, attackers);

      stm = ~stm; // Switch side to move

      // Negamax the balance with alpha = balance, beta = balance+1 and
      // add nextVictim's value.
      //
      //      (balance, balance+1) -> (-balance-1, -balance)
      //
      assert(balance < VALUE_ZERO);

      balance = -balance - 1 - PieceValue[MG][nextVictim];

      // If balance is still non-negative after giving away nextVictim then we
      // win. The only thing to be careful about it is that we should revert
      // stm if we captured with the king when the opponent still has attackers.
      if (balance >= VALUE_ZERO)
      {
          if (nextVictim == KING && (attackers & pieces(stm)))
              stm = ~stm;
          break;
      }
      assert(nextVictim != KING);
  }
  return us != stm; // We break the above loop when stm loses
}


/// Position::is_draw() tests whether the position is drawn by 50-move rule
/// or by repetition. It does not detect stalemates.

bool Position::is_draw(int ply) const {

  /* // 64-move rule
  if (st->rule50 > 127 && (!checkers() || MoveList<LEGAL>(*this).size()))
      return true; */ // redghost

  // Return a draw score if a position repeats once earlier but strictly
  // after the root, or repeats twice before or at the root.
  if (st->repetition && st->repetition < ply)
      return true;

  return false;
}


// Position::has_repeated() tests whether there has been at least one repetition
// of positions since the last capture or pawn move.

bool Position::has_repeated() const {

    StateInfo* stc = st;
    int end = std::min(st->rule50, st->pliesFromNull);
    while (end-- >= 4)
    {
        if (stc->repetition)
            return true;

        stc = stc->previous;
    }
    return false;
}


/// Position::has_game_cycle() tests if the position has a move which draws by repetition,
/// or an earlier position has a move that directly reaches the current position.

bool Position::has_game_cycle(int ply) const {

  int j;

  int end = std::min(st->rule50, st->pliesFromNull);

  if (end < 3)
    return false;

  Key originalKey = st->key;
  StateInfo* stp = st->previous;

  for (int i = 3; i <= end; i += 2)
  {
      stp = stp->previous->previous;

      Key moveKey = originalKey ^ stp->key;
      if (   (j = H1(moveKey), cuckoo[j] == moveKey)
          || (j = H2(moveKey), cuckoo[j] == moveKey))
      {
          Move move = cuckooMove[j];
          Square s1 = from_sq(move);
          Square s2 = to_sq(move);

          if (!(between_bb(s1, s2) & pieces()))
          {
              if (ply > i)
                  return true;

              // For nodes before or at the root, check that the move is a
              // repetition rather than a move to the current position.
              // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in
              // the same location, so we have to select which square to check.
              if (color_of(piece_on(empty(s1) ? s2 : s1)) != side_to_move())
                  continue;

              // For repetitions before or at the root, require one more
              if (stp->repetition)
                  return true;
          }
      }
  }
  return false;
}


/// Position::flip() flips position with the white and black sides reversed. This
/// is only useful for debugging e.g. for finding evaluation symmetry bugs.

void Position::flip() {

  string f, token;
  std::stringstream ss(fen());

  for (Rank r = RANK_8; r >= RANK_1; --r) // Piece placement
  {
      std::getline(ss, token, r > RANK_1 ? '/' : ' ');
      f.insert(0, token + (f.empty() ? " " : "/"));
  }

  ss >> token; // Active color
  f += (token == "w" ? "B " : "W "); // Will be lowercased later

  std::transform(f.begin(), f.end(), f.begin(),
                 [](char c) { return char(islower(c) ? toupper(c) : tolower(c)); });

  std::getline(ss, token); // Half and full moves
  f += token;

  set(f, is_chess960(), st, this_thread());

  assert(pos_is_ok());
}


/// Position::pos_is_ok() performs some consistency checks for the
/// position object and raises an asserts if something wrong is detected.
/// This is meant to be helpful when debugging.

bool Position::pos_is_ok() const {

  constexpr bool Fast = true; // Quick (default) or full check?

  if (   (sideToMove != WHITE && sideToMove != BLACK)
      || piece_on(square<KING>(WHITE)) != W_KING
      || piece_on(square<KING>(BLACK)) != B_KING)
      assert(0 && "pos_is_ok: Default");

  if (  ((pieces(WHITE , PAWN) & (Rank1BB | Rank2BB | Rank6BB | Rank7BB | Rank8BB)) //redghost
      && (pieces(BLACK , PAWN) & (Rank8BB | Rank7BB | Rank3BB | Rank2BB | Rank1BB)))
      || pieceCount[W_PAWN] > 8
      || pieceCount[B_PAWN] > 8)
      assert(0 && "pos_is_ok: Pawns");

  if (Fast)
      return true;

  if (   pieceCount[W_KING] != 1
      || pieceCount[B_KING] != 1
      || attackers_to(square<KING>(~sideToMove)) & pieces(sideToMove))
      assert(0 && "pos_is_ok: Kings");

  if (   (pieces(WHITE) & pieces(BLACK))
      || (pieces(WHITE) | pieces(BLACK)) != pieces()
      || popcount(pieces(WHITE)) > 16
      || popcount(pieces(BLACK)) > 16)
      assert(0 && "pos_is_ok: Bitboards");

  for (PieceType p1 = PAWN; p1 <= KING; ++p1)
      for (PieceType p2 = PAWN; p2 <= KING; ++p2)
          if (p1 != p2 && (pieces(p1) & pieces(p2)))
              assert(0 && "pos_is_ok: Bitboards");

  StateInfo si = *st;
  set_state(&si);
  if (std::memcmp(&si, st, sizeof(StateInfo)))
      assert(0 && "pos_is_ok: State");

  for (Piece pc : Pieces)
  {
      if (   pieceCount[pc] != popcount(pieces(color_of(pc), type_of(pc)))
          || pieceCount[pc] != std::count(board, board + SQUARE_NB, pc))
          assert(0 && "pos_is_ok: Pieces");

      for (int i = 0; i < pieceCount[pc]; ++i)
          if (board[pieceList[pc][i]] != pc || index[pieceList[pc][i]] != i)
              assert(0 && "pos_is_ok: Index");
  }

  return true;
}

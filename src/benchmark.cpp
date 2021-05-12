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

#include <fstream>
#include <iostream>
#include <istream>
#include <vector>

#include "position.h"

using namespace std;

namespace {

const vector<string> Defaults = {
  "rnsmksnr/8/pppppppp/8/8/PPPPPPPP/8/RNSKMSNR w 0 1",
  "r3k2r/2ms1s2/ppn1ppnp/2ppP3/3P1M2/PPPS1NPP/5S2/RN1K3R b 0 2", // high redghost
  "r3k2r/2ms1s2/ppn1ppnp/3pP3/3P1M2/PP1S1NPP/5S2/RN1K3R b 0 3",
  "r3k2r/2msns2/ppn1pp1p/2ppP3/3P1M2/PPP2NPP/2S2S2/RN1K3R b 0 2",
  "r3k2r/2msns2/ppn1pp1p/3pP3/3P1M2/PP3NPP/2S2S2/RN1K3R b 0 3",
  "r3k2r/3sns2/ppnmpp1p/3pP3/3P1M2/PP1S1NPP/2KNS3/R6R b 0 2",
  "r3k2r/3sns2/ppnmpp1p/2ppP3/3P1M2/PPPS1NPP/2KNS3/R6R b 0 1",
  "r3k1nr/3s1s2/ppn2ppp/2m1p3/3pPP2/P1PP1NPP/2SNSM2/1R1K3R w 0 2",
  "r3k1nr/3s1s2/ppn3pp/2m1p3/3pP3/P1PP1NPP/2SNSM2/1R1K3R w 0 3",
  "r3ksnr/2s5/ppn2ppp/2m1p3/3pPP2/P1PP1NPP/2SNSM2/R2K3R w 0 2",
  "r3ksnr/2s5/ppn3pp/2m1p3/3pP3/P1PP1NPP/2SNSM2/R2K3R w 0 3",
  "r1s1k1nr/5s2/ppn2ppp/2mpp3/5P2/P1PPPNPP/2SNSM2/R2K3R w 0 2",
  "r1s1k2r/2m1ns2/ppnppp1p/8/3PPM2/PP3NPP/5S2/RNSK3R b 0 3",
  "r3k2r/2ms1s2/ppn1ppnp/2ppP3/3P1M2/PPP2NPP/2SN1S2/R2K3R b 0 2",
  "r3k2r/2ms1s2/ppn1ppnp/3pP3/3P1M2/PP3NPP/2SN1S2/R2K3R b 0 3",
  "r1smk2r/4ns2/ppnppp1p/8/3PPM2/PP3NPP/8/RNSK1S1R b 0 1",
  "r1s1k2r/2m1ns2/ppnppp1p/8/3PPM2/PP3NPP/5S2/RNSK3R b 0 3",
  "r1s1k2r/2m1ns2/ppnppp1p/2p5/3PPM2/PPP2NPP/5S2/RNSK3R b 0 2", // high redghost
  // test kbq knq knqq redghost
  "7k/5KM1/6M1/6M1/8/8/8/8 b 0 23",    // kqqqk corner2
  "1k6/1MM5/1MK5/8/8/8/8/8 b 0 1",     // kqqqk corner1
  "8/8/8/8/8/1KN5/1M6/1k6 b 0 27",     // knqk
  "8/8/8/8/8/KN6/2M5/k2M4 b 0 13",     // knqqk
  "8/8/8/8/8/KN6/2M5/2kM4 b 0 13",     // knqqk
  "8/8/8/1N6/8/1M6/1MK5/k7 b 0 8",     // knqqk
  "6k1/6S1/6S1/8/8/5K2/8/8 b 0 20",    // kbbk
  "2k5/2S5/1S2K3/8/8/8/8/8 b 0 15",    // kbbk
  "8/8/8/8/2M5/2S5/k7/2K5 b 1 1",      // kbqk
  "8/8/8/8/2M5/k1S5/8/1K6 b 3 2",      // kbqk
  "8/8/8/8/k1M5/2S5/1K6/8 b 5 3",      // kbqk
  "8/8/8/k7/2M5/1KS5/8/8 b 7 4",       // kbqk
  "8/6Sk/5KM1/8/8/8/8/8 b 0 24",       // kbqk
  "8/8/8/8/2M5/k1S5/8/1K6 b 1 1",      // kbqk
  "1k6/1S3M2/1K6/8/8/8/8/8 b 0 10",    // kbqk
  "8/8/8/8/k1M5/2S5/1K6/8 b 3 2"       // kbqk
};

} // namespace

/// setup_bench() builds a list of UCI commands to be run by bench. There
/// are five parameters: TT size in MB, number of search threads that
/// should be used, the limit value spent for each position, a file name
/// where to look for positions in FEN format and the type of the limit:
/// depth, perft, nodes and movetime (in millisecs).
///
/// bench -> search default positions up to depth 13
/// bench 64 1 15 -> search default positions up to depth 15 (TT = 64MB)
/// bench 64 4 5000 current movetime -> search current position with 4 threads for 5 sec
/// bench 64 1 100000 default nodes -> search default positions for 100K nodes each
/// bench 16 1 5 default perft -> run a perft 5 on default positions

vector<string> setup_bench(const Position& current, istream& is) {

  vector<string> fens, list;
  string go, token;

  // Assign default values to missing arguments
  string ttSize    = (is >> token) ? token : "16";
  string threads   = (is >> token) ? token : "1";
  string limit     = (is >> token) ? token : "13";
  string fenFile   = (is >> token) ? token : "default";
  string limitType = (is >> token) ? token : "depth";

  go = "go " + limitType + " " + limit;

  if (fenFile == "default")
      fens = Defaults;

  else if (fenFile == "current")
      fens.push_back(current.fen());

  else
  {
      string fen;
      ifstream file(fenFile);

      if (!file.is_open())
      {
          cerr << "Unable to open file " << fenFile << endl;
          exit(EXIT_FAILURE);
      }

      while (getline(file, fen))
          if (!fen.empty())
              fens.push_back(fen);

      file.close();
  }

  list.emplace_back("setoption name Threads value " + threads);
  list.emplace_back("setoption name Hash value " + ttSize);
  list.emplace_back("ucinewgame");

  for (const string& fen : fens)
      if (fen.find("setoption") != string::npos)
          list.emplace_back(fen);
      else
      {
          list.emplace_back("position fen " + fen);
          list.emplace_back(go);
      }

  return list;
}

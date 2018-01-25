#!/bin/bash
# verify perft numbers (positions from https://chessprogramming.wikispaces.com/Perft+Results)

error()
{
  echo "perft testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

echo "perft testing started"

cat << EOF > perft.exp
   set timeout 10
   lassign \$argv pos depth result
   spawn ./stockfish
   send "position \$pos\\ngo perft \$depth\\n"
   expect "Nodes searched? \$result" {} timeout {exit 1}
   send "quit\\n"
   expect eof
EOF

expect perft.exp startpos 5 6223994 > /dev/null

rm perft.exp

echo "perft testing OK"

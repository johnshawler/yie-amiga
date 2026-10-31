# sourced by run.sh inside Xvfb: key/shot/full are defined there.  Needs the DIAG HUD build (row 0 frame, 12 input bits, 13 game state bytes $51C3..$51C6).
snap() { key keydown F1; sleep 1.0; full "$1"; key keyup F1; sleep 0.4; }
snap a0; sleep 3; snap a1                       # attract mode: frames advance
tap() { key keydown $1; sleep 0.3; key keyup $1; }
tap 5; sleep 1.5; snap coin                 # coin
tap 1; sleep 4; snap start                  # start 1P
key keydown Right; key keydown z; sleep 1.5; snap joy; key keyup Right; key keyup z      # joystick: right + fire
key keydown x; sleep 1.5; snap btn2; key keyup x                                          # second button
full pre; sleep 1.5; full mov   # (two pictures of the running game, 1.5 s apart: they differ)
tap p; sleep 2; full pz1; sleep 2; full pz2; tap p; sleep 2; snap resume    # pause: two pictures 2 s apart are identical; resume
tap Escape; sleep 14; snap restart          # Esc restarts the machine: attract mode again, no credit
tap z; sleep 5; snap freeplay               # fire with no credit: coin + start (AUTO_COIN)

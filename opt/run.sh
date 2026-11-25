#!/bin/bash
for g in 50; # 100 200 300 400 500 600; do #distances between sdrs
    ./build/transmitter --tx-serial 31993E7 --tx-channels 2 --packets 50 &
    TX_PID=$!
    sleep 3                    
    ./build/receiver --rx-serial 31993F0 --rx-channels 2 --label "${g}cm" --packets 50 
    wait $TX_PID
    done
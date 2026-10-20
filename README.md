# SDR-project
SDR for wireless communication, with acceleration on FPGA with the USRP B210 Ettus board. 


# BASELINE
## Build
```
cp ~/SDR-project/baseline
cmake -S . -B build
cmake --build build
```
## Run
```
./build/sdr_tx
```
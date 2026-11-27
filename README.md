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

# OPT
## Build 
```
cp ~/SDR-project/opt
cmake -S . -B build
cmake --build build
```

## Run - 1 Computer
```
Usage normal run: ./txrx --tx-serial <TX_SERIAL> --tx_channels <1 or 2>--rx-serial <RX_SERIAL> --rx_channels <1 or 2> --label <text> --input <number/filepath>

Usage recording run: ./txrx --tx-serial <TX_SERIAL> --tx_channels <1 or 2>--rx-serial <RX_SERIAL> --rx_channels <1 or 2> --label <text> --input <number/filepath> --record

Usage replay run: ./txrx --replay <filepath>

Options:
  --loopback               Run a loopback test without SDR hardware
  --loopback-noise         Run loopback test with added noise
  --replay <filepath>      Run a replay test of recorded IQ-samples, without SDR hardware
  --record                 Record raw IQ-samples
  --payload-size           Payload size of each packet (default is 32 bytes)
  --tx-serial <SERIAL>     Specify the serial number of the TX SDR
  --rx-serial <SERIAL>     Specify the serial number of the RX SDR
  --tx-channels <number>   Number of TX channels, 1 channel = TX/RX A and 2 channels = TX/RX A & TX/RX B
  --rx-channels <number>   Number of RX channels, 1 channel = RX2 A and 2 channels = RX2 A & RX2 B
  --label <text>           Experiment name stored in every CSV row
  --input <filepath or number of packets> The input file to transmit or the number of packets sent from the synthetic counter data
  --output <dir>           CSV output directory (default: tests/outlog)```
```

## Run 2 - Computers
### Transmitter
```
Usage: ./transmitter --tx-serial <TX_SERIAL> --tx-channels <number> --input <number>

Options:
  --tx-serial <SERIAL>     Specify the serial number of the TX SDR
  --tx-channels <number>   Number of TX channels, 1 channel = TX/RX A and 2 channels = TX/RX A & TX/RX B
  --input <filepath or number of packets> The input file to transmit or the number of packets sent from the synthetic counter data

```
The transmitter has a 3 s delay once everything is set up before actually transmitting, when this message shows it's approprioate to start the receiver. 

### Receiver
```
Usage: ./receiver --rx-serial <RX_SERIAL> --rx-channels <number> --tx_channels <1 or 2> --label <text> --inpu <number>

Options:
  --rx-serial <SERIAL>     Specify the serial number of the RX SDR
  --rx-channels <number>   Number of RX channels, 1 channel = RX2 A and 2 channels = RX2 A & RX2 B
  --tx-channels <number>   Number of TX channels (only for logging siso, simo, mimo)
  --label <text>           Experiment name stored in every CSV row
```
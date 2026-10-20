# 1. radio/ — talk to the B210

This layer should know about UHD, and ideally nothing above it should.

For example:

```
class UsrpTx {
public:
    void configure(...);
    void send(const std::vector<std::complex<float>>& samples);
};
```

and:
```
class UsrpRx {
public:
    void configure(...);
    size_t receive(
        std::complex<float>* samples,
        size_t count
    );
};
```
The radio layer handles:
    * UHD initialization
    * device selection
    * sample rate
    * center frequency
    * gain
    * bandwidth
    * antenna
    * TX/RX streaming
    * timestamps
    * underruns/overruns

Your PHY should not contain things like:
```
usrp->set_rx_freq(...)
```
Keep UHD isolated here.

# 2. packet_builder — create packets

This is your MAC/network-level representation.

For example, define a packet:
```
┌────────┬────────┬────────┬──────────────┬─────────┐
│ Preamble│ Header │ Length │ Payload      │ CRC     │
└────────┴────────┴────────┴──────────────┴─────────┘
```
You might define:
```
struct Packet {
    uint8_t source;
    uint8_t destination;
    uint8_t sequence;
    std::vector<uint8_t> payload;
};
```
Then:
```
PacketBuilder
```
turns that into bytes:
```
Packet
   ↓
[header bytes]
   +
[payload]
   +
[CRC]
   ↓
std::vector<uint8_t>
```
For example:
```
auto packet = builder.build(
    source,
    destination,
    payload
);
```
The packet builder shouldn't know anything about QPSK, samples, UHD, etc.

# 3. CRC

Have a separate CRC module:
```
packet bytes
     ↓
    CRC
     ↓
packet + CRC

The receiver does:

received bytes
      ↓
CRC check
      ↓
valid / invalid
```
This gives you a very useful first measure of whether the communication system actually works.

# 5. Modulator

The modulator converts bits into complex baseband symbols.

For example, with QPSK:

bits
```
00 → +1 + j
01 → -1 + j
11 → -1 - j
10 → +1 - j
```
Conceptually:
```
std::vector<std::complex<float>>
modulate(const std::vector<uint8_t>& bits);
```
The modulator should not know about UHD.

Its input/output is purely mathematical:
```
bits
 ↓
QPSK
 ↓
complex symbols
```
That makes it extremely easy to unit-test.

# 6. Pulse shaping

The QPSK symbols aren't necessarily what you want to send directly to the B210.

Typically:
```
symbols
   ↓
upsampling
   ↓
root-raised-cosine filter
   ↓
complex IQ samples

For example:

QPSK symbols
       ↓
     RRC
       ↓
1 MS/s complex samples
```
I'd make this a separate module:
```
std::vector<std::complex<float>>
pulse_shape(
    const std::vector<std::complex<float>>& symbols
);
```

# 7. Synchronization

This becomes one of the most important parts of your receiver.

The receiver doesn't know exactly where your packet begins.

You need something like:
```
                    packet
                      │
                      ▼
... noise ... | PREAMBLE | HEADER | DATA | CRC | ...
                ↑
                detect this
```
Your synchronizer detects the preamble.

Initially, make your preamble something simple and recognizable:

1010101010101010...

or, better, a known pseudorandom sequence.

Then:
```
RX samples
    ↓
preamble detector
    ↓
packet start
```
# 8. Matched filter / timing recovery

The receiver gets:
```
RF
 ↓
B210
 ↓
complex samples
```
Those samples aren't automatically aligned to symbol boundaries.

You eventually need:
```
RX samples
    ↓
matched filter
    ↓
timing recovery
    ↓
one sample/symbol
```
This is a separate PHY component.

# 9. Demodulator

The inverse of your modulator:
```
complex symbols
       ↓
     QPSK
       ↓
     bits
```
For example:
```
std::vector<uint8_t>
demodulate(
    const std::vector<std::complex<float>>& symbols
);
```
Again, don't put UHD code here.

# 10. Packet parser

The receiver gets:
```
bits
 ↓
bytes
 ↓
packet parser
```
It should reconstruct:
```
Packet packet;
```
and verify:
```
header
length
payload
CRC
```
Then your application gets a clean packet.
# 11. Main TX program

Eventually your transmitter can become surprisingly simple:
```
int main()
{
    UsrpTx radio;

    radio.configure(...);

    PacketBuilder packet_builder;
    Modulator modulator;
    PulseShaper shaper;

    auto packet = packet_builder.build(
        1,              // source
        2,              // destination
        payload
    );

    auto bits = bytes_to_bits(packet);

    auto symbols = modulator.modulate(bits);

    auto samples = shaper.process(symbols);

    radio.send(samples);
}
```
That's exactly what you want architecturally.

main_tx.cpp should be an orchestrator, not a 2,000-line DSP implementation.
# 12. Main RX program

Similarly:
```
int main()
{
    UsrpRx radio;

    radio.configure(...);

    Synchronizer synchronizer;
    MatchedFilter filter;
    Demodulator demodulator;
    PacketParser parser;

    while (running)
    {
        auto samples = radio.receive();

        auto packet_samples =
            synchronizer.find_packet(samples);

        auto symbols =
            filter.process(packet_samples);

        auto bits =
            demodulator.demodulate(symbols);

        auto packet =
            parser.parse(bits);

        if (packet.valid)
            handle_packet(packet);
    }
}
```
Again, each component has one responsibility.
A very important design decision

Keep these three representations separate:
```
        MAC                    PHY                    RF

   bytes / packets          symbols              IQ samples
        │                      │                      │
        │                      │                      │
        ▼                      ▼                      ▼

 PacketBuilder ───────> Modulator ───────> USRP TX
                                             │
                                             ▼
                                            RF
                                             │
                                             ▼
 USRP RX <──────────── Demodulator <────── samples
     │
     ▼
 PacketParser
```
Don't make your packet builder return complex<float>.

Don't make your modulator know about Packet.

Don't make your UHD class know what a packet is.

That separation will save you a lot of debugging time.
I'd build it in this order

Don't try to implement the entire wireless stack at once.
Phase 1 — radio

Get:
```
B210 #1 → TX IQ → B210 #2 → RX IQ
```
working.

You already have the RX side almost there.
Phase 2 — known signal

Transmit a simple known waveform:

1111000011110000...

and verify that the second B210 receives it.
Phase 3 — QPSK

Implement:
```
bits → QPSK → IQ
```
and:
```
IQ → QPSK → bits
```
Test these without a USRP first.

For example:
```
10011010
 ↓
QPSK modulator
 ↓
complex symbols
 ↓
QPSK demodulator
 ↓
10011010
```
Phase 4 — packet

Introduce:
```
PREAMBLE | HEADER | PAYLOAD | CRC
```
Phase 5 — synchronization

Make the receiver find:
```
-----noise-----PREAMBLE|HEADER|DATA|CRC
                 ↑
                 detect
```
Phase 6 — robustness

Then add:

FEC
interleaving
timing recovery
carrier recovery
frequency offset estimation
automatic gain control

Phase 7 — MAC

Once the physical layer works, implement:
```
TX
 ↓
packet
 ↓
wait for ACK
 ↓
ACK?
 ├── yes → next packet
 └── no  → retransmit
```
At that point you're moving from a PHY demo toward an actual wireless communication system.
One architectural addition I'd strongly recommend

Have a common configuration structure:
```
struct RadioConfig {
    double center_frequency;
    double sample_rate;
    double bandwidth;
    double tx_gain;
    double rx_gain;
    std::string tx_antenna;
    std::string rx_antenna;
};
```
And separately:
```
struct PhyConfig {
    double symbol_rate;
    unsigned samples_per_symbol;
    float rolloff;
    // modulation, coding, etc.
};
```
Then your configuration becomes:
```
RadioConfig
    ↓
USRP

PhyConfig
    ↓
Modulator / Demodulator
```
This lets you change, for example, QPSK → BPSK without touching the UHD code.

For your two B210s, I'd initially target a very simple single-channel BPSK/QPSK packet link with a known preamble, header, payload, and CRC. Once that works reliably, add synchronization and FEC rather than starting with a sophisticated waveform.

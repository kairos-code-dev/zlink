[English](protocol-raw.md) | [한국어](protocol-raw.ko.md)

# RAW (STREAM) Protocol Details

## 1. Overview
A protocol dedicated to the STREAM socket. Used for communication with external clients that do not use ZMP.

## 2. Wire Format
```
┌──────────────────────┬─────────────────────────────┐
│  Length (4 Bytes)    │     Payload (N Bytes)       │
│  (Big Endian)        │                             │
└──────────────────────┴─────────────────────────────┘
```

- Length: Pure payload length (4B Big Endian)
- Payload: Application data

## 3. Design Intent
- Simplify client implementation: `read(4) → read(len)` pattern
- Stream transparency: Minimal framing overhead
- No handshake (immediate data send/receive)

## 4. STREAM Socket Internal API (Multipart)

### 4.1 Send (zlink_send)
```
Frame 1: [Routing ID (4 bytes)] + MORE flag
Frame 2: [Payload (N bytes)]
```

### 4.2 Receive (zlink_recv)
```
Frame 1: [Routing ID (4 bytes)] + MORE flag
Frame 2: [Payload (N bytes)]
```

### 4.3 Event Messages
- Connect: [Routing ID] + MORE, [0x01]
- Disconnect: [Routing ID] + MORE, [0x00]

## 5. Engine Implementation
- Uses asio_raw_engine_t
- raw_encoder_t: routing_id + Length-Prefix encoding
- raw_decoder_t: Length-Prefix → msg_t decoding

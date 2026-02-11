[English](protocol-zmp.md) | [한국어](protocol-zmp.ko.md)

# ZMP v2.0 Protocol Details

## 1. Design Philosophy
- ZMTP-incompatible (optimized exclusively for zlink)
- 8B fixed header (no variable-length encoding)
- Minimal handshake

## 2. Frame Structure

### 2.1 Header Layout (8 Bytes Fixed)
```
 Byte:   0         1         2         3         4    5    6    7
      ┌─────────┬─────────┬─────────┬─────────┬─────────────────────┐
      │  MAGIC  │ VERSION │  FLAGS  │RESERVED │   PAYLOAD SIZE      │
      │  (0x5A) │  (0x02) │         │ (0x00)  │   (32-bit BE)       │
      └─────────┴─────────┴─────────┴─────────┴─────────────────────┘
```

Fields:
| Field | Offset | Size | Description |
|------|--------|------|------|
| MAGIC | 0 | 1 | 0x5A ('Z') |
| VERSION | 1 | 1 | 0x02 |
| FLAGS | 2 | 1 | Frame flags |
| RESERVED | 3 | 1 | 0x00 |
| PAYLOAD SIZE | 4-7 | 4 | Big Endian |

### 2.2 FLAGS Bit Definitions
| Bit | Name | Value | Description |
|------|------|-----|------|
| 0 | MORE | 0x01 | Multipart continuation |
| 1 | CONTROL | 0x02 | Control frame |
| 2 | IDENTITY | 0x04 | Contains Routing ID |
| 3 | SUBSCRIBE | 0x08 | Subscription request |
| 4 | CANCEL | 0x10 | Subscription cancel |

## 3. Handshake

### 3.1 Sequence
```
Client                              Server
   │                                   │
   │─────── HELLO (greeting) ─────────►│
   │◄────── HELLO (greeting) ──────────│
   │─────── READY (metadata) ─────────►│
   │◄────── READY (metadata) ──────────│
   │◄─────── Data Exchange ───────────►│
```

### 3.2 HELLO Frame
- control_type (1B)
- socket_type (1B)
- routing_id_len (1B)
- routing_id (0~255B)

### 3.3 READY Frame
- Socket-Type property (always)
- Identity property (DEALER/ROUTER only)

## 4. WebSocket Framing
- RFC 6455 Binary frame (Opcode=0x02)
- Payload = ZMP Frame
- Based on the Beast library

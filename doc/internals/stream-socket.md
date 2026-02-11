[English](stream-socket.md) | [한국어](stream-socket.ko.md)

# STREAM Socket WS/WSS Optimization

## 1. Overview

The STREAM socket supports RAW communication with external clients (web browsers, game clients, etc.) that do not use ZMP. It supports tcp, tls, ws, and wss transports, with a particular focus on performance optimization of the WS/WSS path.

## 2. Architecture

### 2.1 Component Layout

| Component | File | Role |
|----------|------|------|
| stream_t | src/sockets/stream.cpp | STREAM socket logic |
| raw_encoder_t | src/protocol/raw_encoder.cpp | Length-Prefix encoding |
| raw_decoder_t | src/protocol/raw_decoder.cpp | Length-Prefix decoding |
| asio_raw_engine_t | src/engine/asio/asio_raw_engine.cpp | RAW I/O engine |
| ws_transport_t | src/transports/ws/ | WebSocket transport |
| wss_transport_t | src/transports/ws/ | WebSocket + TLS |

### 2.2 Data Flow

```
Application                Stream Socket              Engine              Transport
    │                          │                        │                     │
    │  zlink_send(rid+data)    │                        │                     │
    │─────────────────────────►│                        │                     │
    │                          │  pipe_t::write()       │                     │
    │                          │───────────────────────►│                     │
    │                          │                        │  raw_encode         │
    │                          │                        │  (4B len + payload) │
    │                          │                        │────────────────────►│
    │                          │                        │                     │  ws::write
```

## 3. WS/WSS Performance Optimization

### 3.1 Read Path Copy Elimination
- Before: Beast flat_buffer → temporary buffer → msg_t (2 copies)
- Optimized: Move directly from Beast flat_buffer to msg_t (1 copy eliminated)

### 3.2 Write Path Copy Elimination
- Before: msg_t → intermediate buffer → Beast write (2 copies)
- Optimized: Pass msg_t data directly to Beast write buffer

### 3.3 Beast Write Buffer Enlargement
- Increased from default 4KB → 64KB
- Enables batch sending of multiple small messages

### 3.4 Frame Fragmentation Disabled
- `auto_fragment(false)` setting
- Single WebSocket frame per message

## 4. Benchmark Results

### 4.1 WS Optimization Effect (1KB Messages)
| Item | Before Optimization | After Optimization | Improvement |
|------|-----------|-----------|--------|
| WS 1KB | 315 MB/s | 473 MB/s | +50% |
| WSS 1KB | 279 MB/s | 382 MB/s | +37% |

### 4.2 Large Message Improvements
| Size | WS Improvement | WSS Improvement |
|------|-----------|-----------|
| 64B | +11% | +13% |
| 1KB | +50% | +37% |
| 64KB | +97% | +54% |
| 262KB | +139% | +62% |

### 4.3 Comparison Against Beast Standalone
| Transport | Beast | zlink | Ratio |
|-----------|-------|-------|------|
| tcp | 1416 MB/s | 1493 MB/s | 105% |
| ws | 540 MB/s | 696 MB/s | 129% |

## 5. Design Trade-offs

- Speculative write not supported (WebSocket is frame-based)
- Gather write supported for WS/WSS (Beast handles internal buffering)
- TLS/WSS has encryption overhead

# Phase 0 Flow Diagram Notes (ZMP v1)

**Date:** 2026-01-22  
**Owner:** 팀장님  
**Status:** Draft

---

## Session/Engine Flow

1) session attach
2) engine plug
3) HELLO 교환
4) READY 교환
5) engine_ready
6) normal IO loop

---

## Message Flow (Send Path)

socket -> pipe -> session -> engine -> encoder -> transport

---

## Message Flow (Recv Path)

transport -> decoder -> engine -> session -> pipe -> socket

---

## Control Frames

- HELLO: 연결 직후 1회 교환
- READY: HELLO 완료 후 1회 교환(메타데이터 optional)
- ERROR: 핸드셰이크 실패 시 1회 송신 후 종료
- HEARTBEAT: 옵션 활성 시 주기 송신

---

## Heartbeat TTL/Context

- TTL/Context 확장형 허용(레거시 1바이트 형식도 허용)
- ACK는 ctx 에코

---

## Reference Sources

- `src/asio/asio_zmp_engine.cpp`
- `src/asio/asio_engine.cpp`
- `src/session_base.cpp`
- `src/pipe.cpp`

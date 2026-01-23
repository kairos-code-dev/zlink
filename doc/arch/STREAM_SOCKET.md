# STREAM Socket Architecture and Optimization Notes

이 문서는 zlink에서 STREAM 소켓을 도입하면서 적용한 설계 방식, 아키텍처 구조,
그리고 WS/WSS 성능 최적화 전략을 정리한 문서입니다.

---

## 1. 목표

- ZMQ 스타일의 API/스레드 모델을 유지하면서 STREAM 소켓 제공
- TCP/TLS/WS/WSS 전송 계층에서 일관된 동작
- WS/WSS 성능을 기존 Beast 단독 성능에 최대한 근접

---

## 2. 프로토콜 설계 (STREAM RAW)

STREAM 소켓은 기존 ZMP와 별도의 **RAW framing**을 사용합니다.

### 2.1 프레임 형식

- **routing_id**: 4 bytes (Big Endian, uint32)
- **payload**: 가변 길이

```
[data message]
+-------------+---------------------+
| routing_id  | payload (N bytes)   |
+-------------+---------------------+
  4 bytes         N bytes
```

### 2.2 이벤트 메시지

- **connect**: [routing_id(4B)] + [0x01]
- **disconnect**: [routing_id(4B)] + [0x00]

```
[connect]
+-------------+---------+
| routing_id  | 0x01    |
+-------------+---------+

[disconnect]
+-------------+---------+
| routing_id  | 0x00    |
+-------------+---------+
```

### 2.3 설계 의도

- STREAM은 메시지 경계를 애플리케이션이 직접 정의해야 하므로
  **RAW length-prefix** 기반으로 통일
- 4B routing_id는 ROUTER 호환성과 처리 비용 사이 타협점

---

## 3. 아키텍처 구조

STREAM 소켓은 기존 zlink 구조에 다음 요소가 추가/확장되었습니다.

### 3.1 주요 컴포넌트

- `src/sockets/stream.cpp/hpp`
  - STREAM 소켓 로직, connect/disconnect 처리

- `src/protocol/raw_encoder.*`, `raw_decoder.*`
  - RAW framing 인코더/디코더 (length-prefix)

- `src/engine/asio/asio_raw_engine.*`
  - RAW 인코더/디코더 기반 엔진

- `src/transports/*`
  - TCP/TLS/WS/WSS 모두 RAW 엔진 사용

### 3.2 데이터 흐름

```
Application
   │
   ▼
STREAM socket (src/sockets/stream)
   │  (msg_t)
   ▼
session_base / pipe / mailbox
   │
   ▼
asio_raw_engine
   │  (raw_encoder/raw_decoder)
   ▼
transport (tcp/tls/ws/wss)
```

### 3.3 WS/WSS의 특이점

- WS/WSS는 **프레임 경계 프로토콜**
- TCP/TLS처럼 “버스트 read + 내부 파싱”이 어렵고
  **Beast가 프레임 단위로 read/write**

---

## 4. WS/WSS 성능 병목과 개선 전략

### 4.1 병목 원인

1) **불필요한 복사**
   - 기존: `flat_buffer → frame_buffer → user buffer`
   - write 경로에서도 `_write_buffer` 복사

2) **Beast 기본 write buffer가 작음 (4KB)**
   - 큰 메시지에서 프레임이 여러 write로 분할

3) **speculative_write 사용 불가**
   - WebSocket은 프레임 단위 write가 블로킹될 위험이 큼

### 4.2 적용된 최적화

#### A) Read 경로 복사 제거
- `flat_buffer`를 유지하고 offset/consume 방식 사용
- 추가 frame buffer 제거

#### B) Write 경로 복사 제거
- `_write_buffer` 제거
- encoder 버퍼를 async_write에 직접 전달

#### C) Beast 내부 write buffer 확대
- `write_buffer_bytes(64 * 1024)` 적용
- 큰 메시지에서 write 호출 횟수 감소

#### D) 프레임 분할 비활성화
- `auto_fragment(false)` 적용

### 4.3 결과 요약

- WS/WSS 모두 **대폭 개선**
- 특히 64KB 이상 구간에서 throughput/latency 급격히 개선

(벤치 결과는 `benchwithzlink/results/20260123/` 참조)

---

## 5. 성능 측정 방식

- 벤치: `benchwithzlink/run_benchmarks.sh`
- 반복: `--runs 10`
- 메시지 크기: 64B ~ 262144B
- 비교: baseline vs current

추가적으로 Beast 단독 비교는 `benchwithbeast/bench_beast_stream.cpp`로 수행

---

## 6. 발생 이슈 및 해결 방식

1) **WS/WSS 성능 급격 저하**
   - 원인: 프레임 기반 read/write, 추가 복사, 작은 Beast write buffer
   - 해결:
     - `flat_buffer`를 offset/consume 방식으로 유지해 **read 복사 1회 제거**
     - async_write 경로에서 `_write_buffer` 제거로 **write 복사 제거**
     - `write_buffer_bytes(64KB)`로 **큰 메시지 write 호출 수 감소**
     - `auto_fragment(false)`로 **프레임 분할 방지**

2) **WS/WSS speculative_write 시 hang**
   - 원인: WebSocket write가 프레임 전체 전송 완료까지 블로킹 가능
   - 해결: WS/WSS는 speculative_write 비활성 유지

3) **async_read_some 기반 구현의 성능 하락**
   - 원인: per-call framing/compose 비용 증가
   - 해결: `async_read`로 프레임 단위 수신 후 offset/consume 처리로 복귀

---

## 7. 설계 상의 트레이드오프

- WS/WSS는 프레임 기반이라 TCP만큼의 배칭 효과가 어렵다
- speculative_write는 I/O 스레드 블로킹 위험으로 미사용 유지
- API 호환성(zmq 스타일)을 유지하기 위해 pipe/session 경로를 유지

---

## 8. 향후 개선 후보

1) WS/WSS read 경로에서 decoder와 buffer 직접 연결 (interface 확장 필요)
2) TLS/WSS SSL 설정 튜닝 (세션 재사용, cipher 선택 등)
3) WS/WSS 전용 batch 정책 재검토

---

## 9. 변경된 주요 파일

- `src/sockets/stream.*`
- `src/engine/asio/asio_raw_engine.*`
- `src/protocol/raw_encoder.*`, `src/protocol/raw_decoder.*`
- `src/transports/ws/*`, `src/transports/tls/wss_transport.*`
- `tests/test_stream_socket.cpp`, `unittests/unittest_raw_decoder.cpp`
- `benchwithzlink/`, `benchwithbeast/`

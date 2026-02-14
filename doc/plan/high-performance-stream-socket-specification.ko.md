# 고성능 stream 소켓 스펙

- 작성일: 2026-02-14
- 상태: 구현용 상세 초안
- 대상: `ZLINK_STREAM`의 고성능 경로(Fastpath) 설계
- 전제:
  - CppServer 바이너리/라이브러리를 직접 재사용하지 않는다.
  - CppServer의 Asio 기반 내부 구조(세션/버퍼/비동기 상태기계)만 차용한다.
  - 성능 우선이며, 기존 인터페이스는 가능한 범위에서 유지한다.

---

## 1. 문서 목적

기존 `STREAM` 스펙(`doc/plan/stream-socket-specification.ko.md`)은 기능 정합 중심으로 작성되어,
고부하(예: `ccu=10000`, `inflight=30`)에서 다음 비용이 누적된다.

- 멀티파트(`routing_id frame + payload frame`) 중심 처리 오버헤드
- 이벤트(`0x01/0x00`)를 데이터 payload와 혼용하는 분기 비용
- 범용 파이프 경유 구조로 인한 메모리 이동/큐 처리 비용

본 문서는 고성능 경로를 위한 신규 규격을 정의한다.

---

## 2. 설계 목표

### 2.1 성능 목표

- `s2` 시나리오 throughput:
  - 1차: `cppserver-s2` 대비 `>= 80%`
  - 목표: `>= 95%`
- 안정성:
  - `drain_timeout=0`
  - `gating_violation=0`
  - `incomplete_ratio <= 0.01`

### 2.2 호환 목표

- 기존 `zlink_send/zlink_recv/zlink_msg_send/zlink_msg_recv` API 시그니처 유지
- 기존 2-step 수신 패턴(`routing_id`, `payload`)은 앱 레벨에서 계속 사용 가능

### 2.3 비목표

- Fastpath v1에서 WS/WSS/TLS/IPC/inproc 완전 지원
- 기존 STREAM의 모든 관용 동작(암묵적 멀티파트 허용 등) 완전 보존

---

## 3. 동작 모드

고성능 경로는 모드 기반으로 분리한다.

### 3.1 모드 정의 (신규 제안)

```c
ZLINK_STREAM_MODE_LEGACY   = 0
ZLINK_STREAM_MODE_FASTPATH = 1
```

소켓 옵션(신규 제안):

```c
ZLINK_STREAM_MODE
```

### 3.2 기본 정책

- 기본값: `FASTPATH` (기존 STREAM 대체)
- 필요 시 `LEGACY`를 명시 설정해 롤백 가능

---

## 4. Fastpath 핵심 원칙

1. 네트워크 프로토콜은 단순 유지: `4-byte body length + body`
2. 내부 전송 단위는 단일 envelope
3. 앱 호환을 위해 수신 시 `routing_id`/`payload` 2-step 인터페이스는 유지
4. wire에는 `msg_t 객체`를 절대 직렬화하지 않고 payload bytes만 사용
5. 세션별 비동기 상태기계 + 이중 송신 버퍼(main/flush) 적용

---

## 5. Wire 프로토콜 (Fastpath v1)

## 5.1 외부 프레이밍

```
+----------------------+-------------------+
| body_len (4B, BE)    | body (N bytes)    |
+----------------------+-------------------+
```

- `body_len`: body 길이(헤더 + payload)
- 엔디안: big-endian

### 5.2 body(envelope) 포맷

```
+---------+---------+-----------+-------------------+
| ver(1B) | type(1B)| flags(2B) | routing_id(4B, BE)|
+---------+---------+-----------+-------------------+
| payload (0..N)                                 ... |
+-----------------------------------------------------+
```

- 고정 헤더 크기: 8 bytes
- `ver`: 현재 `0x01`
- `type`:
  - `0x00`: DATA
  - `0x01`: CONNECT
  - `0x02`: DISCONNECT
  - `0x03`: PING (예약)
  - `0x04`: PONG (예약)
- `flags`: v1에서 매직값 `0x5A4C` (`'Z''L'`)
- `routing_id`: 4-byte peer id

### 5.3 이벤트 인코딩 규칙

- wire에서는 `type`으로 이벤트를 구분한다.
- 앱 호환(기존 코드)용으로 `zlink_recv()` 2번째 프레임에서는 다음과 같이 변환 가능:
  - CONNECT -> payload `0x01`
  - DISCONNECT -> payload `0x00`

즉, wire와 앱 호환 레이어를 분리한다.

---

## 6. 공개 API 규칙

## 6.1 유지되는 API

- `zlink_send()`, `zlink_recv()`
- `zlink_msg_send()`, `zlink_msg_recv()`
- `ZLINK_RCVMORE`, `ZLINK_DONTWAIT`, `ZLINK_SNDMORE`

### 6.2 송신 호환 규칙

기존 패턴:

```c
zlink_send(stream, rid, 4, ZLINK_SNDMORE);
zlink_send(stream, payload, size, 0);
```

Fastpath 내부 동작:

- 위 2단계를 내부 단일 envelope로 결합하여 세션 TX 큐에 enqueue

### 6.3 수신 호환 규칙

기존 패턴:

```c
zlink_recv(stream, rid, 4, 0);      // RCVMORE=1
zlink_recv(stream, payload, n, 0);  // RCVMORE=0
```

Fastpath 내부 동작:

- 네트워크에서 envelope 1개 수신
- 앱 레벨에는 기존 2-step으로 분해하여 노출

---

## 7. `msg_t` 규칙 (중요)

### 7.1 허용

- 앱/내부 인터페이스에서 `msg_t` 사용
- `recv bytes -> msg_t.init_size() -> memcpy` 경로 사용

### 7.2 금지

- `msg_t` 메모리 레이아웃 자체를 wire에 실어 송수신

이유:

- `msg_t`는 포인터/함수포인터/refcount 포함 내부 객체이며 wire 포맷이 아니다.
- 프로세스/플랫폼/ABI 경계에서 안전하지 않다.

---

## 8. 아키텍처

## 8.1 스레드/샤드 모델

- `io_threads` 개수 기준으로 shard 생성
- 각 shard는:
  - accept 처리
  - 세션 read/write 상태기계 실행
  - shard-local session map 관리

### 8.2 세션 객체

세션별 상태(핵심):

- `recv_buffer` (가변, 재사용)
- `send_main_buffer`
- `send_flush_buffer`
- `send_flush_offset`
- `receiving/sending` 플래그
- `routing_id(4B)` + `session_key(64b)`

### 8.3 큐 모델

- 앱 스레드 -> shard: MPSC 명령 큐(송신/종료/옵션 변경)
- shard 내부: lock 최소화
- 세션 write는 `TrySend()` 재진입 구조로 처리

---

## 9. 송수신 상태기계

## 9.1 Read path

1. `async_read_some()`로 bytes 수신
2. 프레이머가 `4B len` 파싱
3. body 누적 완료 시 envelope 해석
4. `{type, routing_id, payload}`를 수신 큐로 push
5. 앱 레벨 recv 시 2-step으로 분해 전달

### 9.2 Write path

1. 앱 송신 요청을 envelope로 인코딩
2. `send_main_buffer` append
3. flush 버퍼가 비어 있으면 swap(main <-> flush)
4. `async_write_some()` 반복, partial write는 `offset`으로 이어쓰기
5. flush 완료 후 다음 main swap

---

## 10. 라우팅 ID

## 10.1 외부 표현

- 고정 4-byte (`uint32`, BE)

### 10.2 내부 표현

- `session_key`는 64-bit 단조 증가 값 권장
- `routing_id`는 외부/API 호환용 32-bit 매핑 값

### 10.3 매핑 구조

- `routing_id -> session_key`
- `session_key -> session*`

stale send 방지:

- `routing_id` lookup 후 `session_key` 일치 검증

---

## 11. 연결 이벤트

### 11.1 발생 조건

- 세션 활성화 시 CONNECT
- 세션 종료 시 DISCONNECT

### 11.2 순서 보장

- 동일 세션 내 이벤트/데이터 순서는 보장
- 세션 간 전역 총순서는 보장하지 않음

### 11.3 앱 호환 표현

- 기존 앱 호환 필요 시:
  - CONNECT -> payload `0x01`
  - DISCONNECT -> payload `0x00`

---

## 12. 소켓 옵션 정책

## 12.1 Fastpath에서 유지

- `ZLINK_MAXMSGSIZE`
- `ZLINK_SNDHWM` / `ZLINK_RCVHWM`
- `ZLINK_SNDBUF` / `ZLINK_RCVBUF`
- `ZLINK_LINGER`
- `ZLINK_RCVTIMEO` / `ZLINK_SNDTIMEO`
- `ZLINK_TCP_KEEPALIVE*`
- `ZLINK_BACKLOG`
- `ZLINK_EVENTS`
- `ZLINK_LAST_ENDPOINT`

### 12.2 Fastpath에서 축소/변경

- `ZLINK_CONNECT_ROUTING_ID`:
  - STREAM에서는 지원하지 않음 (`EOPNOTSUPP`)
  - routing_id는 서버 자동 할당만 사용

- `ZLINK_ROUTING_ID`:
  - "현재 peer id 조회" 의미로 사용하지 않는다.
  - 로컬 소켓 라우팅 ID 옵션 본래 의미만 유지

### 12.3 신규 제안 옵션

- `ZLINK_STREAM_MODE` (`LEGACY`/`FASTPATH`)
- `ZLINK_STREAM_FASTPATH_STRICT`:
  - `1`: 비지원 트랜스포트 사용 시 즉시 실패
  - `0`: 자동 LEGACY fallback

---

## 13. 트랜스포트 정책

### 13.1 Fastpath v1

- 필수 지원: `tcp://`

### 13.2 Fastpath v1 비대상

- `tls://`, `ws://`, `wss://`, `ipc://`, `inproc://`

비대상 처리:

- `STRICT=1`: `ENOTSUP`
- `STRICT=0`: LEGACY 경로 fallback

---

## 14. 에러 모델

송신:

- `EINVAL`: 잘못된 envelope/routing_id
- `EHOSTUNREACH`: routing_id에 대응하는 세션 없음
- `EAGAIN`: 세션 큐 포화 또는 nonblocking에서 즉시 처리 불가

수신:

- `EAGAIN`: 수신 가능한 envelope 없음
- `EMSGSIZE`: `MAXMSGSIZE` 초과
- `ENOMEM`: 버퍼 확보 실패

연결/세션:

- `ECONNRESET`/`EPIPE`: peer 종료

---

## 15. 기존 스펙 대비 변경 요약

아래는 `stream-socket-specification.ko.md` 대비 Fastpath 기준 변경점이다.

1. 내부 처리 단위를 "2-프레임 멀티파트"에서 "단일 envelope"로 변경
2. 이벤트를 payload 값(0x01/0x00) 혼용에서 `type` 필드 기반으로 변경
3. `CONNECT_ROUTING_ID`를 STREAM에서 비지원으로 변경
4. 트랜스포트를 v1에서 TCP 중심으로 축소
5. Fair Queue 의무를 제거하고 shard/session 기반 스케줄링으로 변경
6. 라우팅 ID는 외부 4-byte 유지, 내부 식별은 별도 64-bit 키 허용
7. `msg_t`는 wire 포맷이 아니며 payload 운반 객체로만 사용

---

## 16. 구현 체크리스트

### 16.1 프로토콜/파서

- [ ] `len(4B BE)+body` 프레이머 구현
- [ ] envelope v1 헤더 파서/검증
- [ ] DATA/CONNECT/DISCONNECT 디스패치

### 16.2 세션 I/O

- [ ] 세션 read loop 비동기 상태기계
- [ ] 세션 write loop(main/flush, partial write) 구현
- [ ] send/recv 버퍼 재사용 전략 적용

### 16.3 라우팅/큐

- [ ] `routing_id <-> session` 매핑
- [ ] MPSC ingress 큐 + shard dispatch
- [ ] 세션 종료 시 맵/큐 정리

### 16.4 API 호환 브릿지

- [ ] 기존 `zlink_send` 2-step 입력을 envelope로 결합
- [ ] 기존 `zlink_recv` 2-step 출력을 envelope에서 분해
- [ ] `RCVMORE` 동작 호환

### 16.5 옵션/정책

- [ ] `ZLINK_STREAM_MODE` 구현
- [ ] strict/fallback 정책 구현
- [ ] STREAM `CONNECT_ROUTING_ID` 비지원(`EOPNOTSUPP`) 정책 반영

---

## 17. 테스트 규격

## 17.1 기능 테스트

- [ ] `s0`: 단일 연결 echo 정합
- [ ] `s1`: 대량 connect/disconnect 정합
- [ ] `s2`: 대량 동시 트래픽 정합
- [ ] 이벤트 순서/중복/유실 검증
- [ ] routing_id 충돌/재사용 안정성 검증

### 17.2 회귀 테스트

- [ ] LEGACY 모드 기존 STREAM 테스트 통과
- [ ] FASTPATH 모드 신규 테스트 통과

### 17.3 성능 테스트

- [ ] 고정 프로파일(`ccu=10000`, `inflight=30`, `size=1024`) 측정
- [ ] `cppserver/asio/dotnet/net-zlink` 대비 비율 산출
- [ ] `drain_timeout`, `gating_violation`, `incomplete_ratio` 확인

---

## 18. 수용 기준 (Acceptance)

다음 조건을 모두 만족하면 Fastpath v1 수용:

1. 기능:
   - `s0/s1/s2` PASS
2. 안정성:
   - `drain_timeout=0`
   - `gating_violation=0`
3. 성능:
   - `zlink-fastpath-s2 >= 0.8 * cppserver-s2`
4. 호환:
   - LEGACY 모드 기존 테스트 회귀 없음

---

## 19. 구현 파일 권장 배치

- 소켓 레이어:
  - `core/src/sockets/stream_fast.cpp`
  - `core/src/sockets/stream_fast.hpp`
- 프로토콜:
  - `core/src/protocol/stream_fast_envelope.hpp`
  - `core/src/protocol/stream_fast_framer.cpp`
- 테스트:
  - `core/tests/test_stream_fastpath.cpp`
  - `core/tests/scenario/stream/zlink/*` (fastpath 모드 실행 추가)

---

## 20. 최종 정리

본 스펙은 "기존 STREAM 인터페이스를 최대한 유지하면서, 내부를 CppServer형 Asio 고성능 구조로 교체"하는 문서다.

핵심은 아래 3가지다.

1. wire는 계속 `4B + body`
2. body는 단일 envelope(`type + routing_id + payload`)
3. `msg_t`는 내부 전달 객체로만 사용

이 원칙을 지키면 기존 앱 영향은 최소화하면서도 성능 목표에 도달할 수 있다.

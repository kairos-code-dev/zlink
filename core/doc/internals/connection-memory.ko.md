[English](connection-memory.md) | [한국어](connection-memory.ko.md)

# 연결당 메모리 구조

이 문서는 transport 연결(TCP/TLS/WS/IPC) 하나가 만들어질 때 core가 무엇을
언제 할당하는지, 그리고 그 크기를 정하는 설계 결정을 설명한다. 서버 간
연결을 수천~수만 개 유지하는 배치에서는 연결당 고정 비용이 프로세스
메모리의 대부분을 차지하므로, 이 구조를 알아야 튜닝 지점을 판단할 수 있다.

사용자 관점의 용량 계획(패턴별 실측치, 규모 산정)은
[성능 가이드의 대량 연결 메모리 계획](../guide/10-performance.ko.md#8-대량-연결-메모리-계획)을
본다. 이 구조를 도출한 측정 실험과 원본 데이터는
`core/study/connection-memory-study.ko.md`에 있다.

## 1. 연결 하나의 할당 사슬

연결이 accept(또는 connect)되면 다음 순서로 객체가 생긴다.

```text
listener accept / connecter 완료
  └─ asio_zmp_engine_t 생성        (raw STREAM이면 asio_raw_engine_t)
       └─ session_base_t 생성
            └─ engine plug → 핸드셰이크
                 ├─ HELLO 교환 직후 encoder/decoder 할당
                 └─ engine_ready → pipepair()
                      = pipe_t 2개 + ypipe 2개 (소켓 ↔ 세션 큐)
```

### 1.1 항목별 할당 (ZMP 연결, 기본 옵션, x86-64)

| 항목 | 할당 시점 | 크기 공식 | 기본값 (B) |
|------|----------|----------|-----------|
| ypipe 큐 청크 ×2 | pipepair 생성자 (즉시) | `session_pipe_granularity(64) × sizeof(msg_t)(64) + 16` ×2 | 8,224 |
| ZMP decoder 버퍼 | HELLO 수신 직후 (핸드셰이크 도중) | `in_batch_size(8192) + 8 + ceil(8192/41) × 40` | 16,200 |
| ZMP encoder 버퍼 | HELLO 수신 직후 (핸드셰이크 도중) | `out_batch_size(8192)` | 8,192 |
| 핸드셰이크 read_buffer | 첫 핸드셰이크 read (지연) | `handshake_read_buffer_size` | 512 |
| asio_zmp_engine_t 객체 | accept 시 | handler_allocator 3×1,040 + options_t 사본(936) + HELLO 272×2 + rid 256 포함 | 5,856 |
| pipe_t 객체 ×2 | pipepair | stream packet 상태·msg_t 2개 내장 | 1,280 |
| session_base_t 객체 | accept 시 | options_t 사본(936) 포함 | 1,216 |
| ypipe_t 객체 ×2 / transport / metadata / ROUTER 라우팅 항목 등 | 각각 | | ~1,300 |
| **합계** | | | **≈ 43 KB** |

raw STREAM 연결은 다르다. STREAM 소켓은 생성자에서 배치 크기를
`in_batch_size = 4,160`, `out_batch_size = 4,096`으로 줄여 쓰므로 decoder
4,208 B / encoder 4,096 B이고, decoder를 plug 시점에 만들기 때문에
핸드셰이크 read_buffer는 아예 할당되지 않는다. HELLO/rid 버퍼도 없다.

할당과 실제 점유(RSS)는 다르다. malloc된 페이지는 touch되어야 커밋되므로
idle 연결의 RSS는 합계보다 작고, 트래픽이 지나가며 점차 합계에 가까워진다.
라이브러리는 연결의 버퍼·청크를 연결 수명 동안 유지하므로 트래픽이 끝나도
RSS가 idle 값으로 되돌아오지 않는다(실측으로 확인) — 용량 계획은 잔류
기준으로 잡아야 하는 이유다(가이드 문서 참고).

## 2. 설계 결정

### 2.1 세션 pipe는 작은 청크를 쓴다 (`session_pipe_granularity = 64`)

ypipe 큐는 청크 단위로 메모리를 할당한다. 기본
`message_pipe_granularity = 256`은 소수의 고처리량 pipe(특히 inproc)를
가정한 값으로, 청크 하나가 16,400 B다. 그런데 transport 연결의 pipe는
사정이 다르다.

- 연결마다 pipepair 하나(청크 2개)가 생성 즉시 할당된다. 연결이 수만 개면
  이 고정 비용이 지배적이다 — 이것이 주 근거다.
- SPOT mesh 내부 소켓(`mesh-pub`/`mesh-xsub`/`external-router`)에서는
  auto-HWM의 연결 수 bucket이 대규모 연결에서 pipe당 예산을 16~32
  메시지까지 줄이므로, 256칸 청크는 실제로 쓸 수 있는 깊이보다 8~16배
  크다.

`pipepair()`는 `session_pipe_` 인자를 받아 세션↔소켓 pipe에만
`session_pipe_granularity = 64`(청크 4,112 B ≈ 1페이지)를 쓴다. inproc
pipe는 256을 사용한다. pipe는 생성할 때 선택한 granularity를
`pipe_t::_session_pipe`에 기록한다. 재연결 시 `hiccup()`도 이 값에 따라
inpipe를 같은 크기로 생성하므로 세션 pipe는 64를 계속 사용한다.

큐가 깊어지면 작은 청크의 할당과 해제가 잦아질 수 있으나, yqueue의
spare-chunk 캐시가 정상 흐름을 흡수한다. tcp/1024 처리량·latency 조건은
study 문서 §6.8의 측정 기록으로 검증한다.

### 2.2 핸드셰이크 read_buffer는 작게, 지연 할당한다

엔진의 `_pipeline.read_buffer`는 decoder가 아직 없는 프로토콜
핸드셰이크 구간에서만 read 대상이 된다. 첫 핸드셰이크 read에서
`handshake_read_buffer_size = 512` B를
확보한다(`select_handshake_read_buffer()`). ZMP HELLO는 증분 파싱이라
(수신 바이트를 `_hello_recv` 누적 버퍼로 옮겨 조립) 버퍼가 작아도
정확성에는 영향이 없고, 핸드셰이크 구간에서 read 횟수가 몇 번 늘 수 있을
뿐이다. decoder가 생성된 뒤의 새 read는 decoder 버퍼로 들어간다. STREAM
소켓이 backpressure 상태이면 pending 버퍼 풀을 사용한다. raw 엔진은
plug에서 decoder를 먼저 만들기 때문에 핸드셰이크 read buffer를 할당하지 않는다.

핸드셰이크 꼬리에 데이터 프레임이 함께 도착하면 잔여 바이트(`_insize > 0`)는
소진될 때까지 이 버퍼 안을 가리킨다. 엔진은 잔여 입력을 모두 소진할 때까지
버퍼의 수명을 유지한다.

## 3. auto-HWM과 메모리의 관계

auto-HWM은 선할당이 아니라 상한(cap)이다. idle 메모리에는 영향이 없고,
부하 시 pipe에 쌓일 수 있는 메시지 수만 제한한다. 연결 수가 늘면 bucket이
pipe당 예산을 줄여 총 노출의 기울기를 제한하며, bucket 전환에는 잦은 왕복을
막는 hysteresis를 적용한다. 자세한 정책은
[소켓 옵션 기본값](socket-option-defaults.ko.md)을 본다.

집행이 메시지 개수 기준이라 message unit(4 KiB)보다 큰 메시지에서는
바이트 노출이 예산을 넘을 수 있다는 한계와, 전역(context 단위) 바이트
예산이 없다는 점은 후속 설계 항목으로 남아 있다(study 문서 §6.7).

## 4. 코드 위치

| 무엇 | 어디 |
|------|------|
| granularity 상수 | `src/runtime/utils/config.hpp` (`message_pipe_granularity`, `session_pipe_granularity`) |
| pipepair와 granularity 선택 | `src/runtime/core/pipe.cpp` (`pipepair`, `pipe_t::hiccup`) |
| 세션 pipe 생성 지점 | `src/runtime/core/session_base.cpp` (`engine_ready`), `src/runtime/sockets/common/socket_base_endpoint.cpp` (connect 경로) |
| 핸드셰이크 버퍼 | `src/runtime/engine/asio/asio_engine.hpp` (`handshake_read_buffer_size`), `asio_engine.cpp` (`select_handshake_read_buffer`) |
| 코덱 버퍼 크기 | `src/runtime/core/options.cpp` (`in/out_batch_size`), `src/runtime/sockets/stream/stream.cpp` (STREAM 축소값) |
| decoder 할당 공식 | `src/runtime/protocol/decoder_allocators.cpp` |

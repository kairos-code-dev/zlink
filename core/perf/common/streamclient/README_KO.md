# Core PERF STREAM 클라이언트

이 디렉터리는 multi STREAM 클라이언트만 포함한다.

## 파일 구성

- `perf_stream_client.cpp`: 진입점 + 실행기 (`main()`, `perf_stream_client_run`)
- `perf_stream_common.hpp`: 공유 상수 (`inline constexpr`), 프로토콜 헬퍼 (len32be 인코딩/디코딩), 시간, 파싱 유틸리티
- `perf_stream_arg_reader.hpp`: `arg_reader_t` CLI 인자 파싱 클래스
- `perf_stream_bench_client_iface.hpp`: `bench_client_iface_t` 순수 가상 인터페이스 (세션/벤치마크 계약)
- `perf_stream_client_options.hpp`: `client_options_t`, `case_metrics_t`, 옵션 파싱, 결과 출력 헬퍼
- `perf_stream_client_session.hpp`: 비동기 상수 (`inline constexpr`), `phase_mode_t`, `resize_latch_t`, `client_session_t`
- `perf_stream_bench_client.hpp`: `loopback_bind_plan_t` + 헬퍼, `bench_client_t` 비동기 벤치마크 오케스트레이터
- `stream_client.hpp`: `stream_client_t` 헤더 전용 len32be 전송 클라이언트 (tcp/tls/ws/wss)

## 아키텍처 개요

```
main()
  └─ perf_stream_client_run()
       ├─ parse_options()
       └─ bench_client_t::run()        ← 비동기 다중 연결 경로
            ├─ io_context + 워커 스레드
            └─ client_session_t (×CCU) ← Boost.Asio 비동기 I/O
```

## Include 의존성 그래프

```
perf_stream_client.cpp
  ├─ perf_stream_bench_client.hpp        (bench_client_t, 루프백 플랜)
  │    ├─ perf_stream_client_options.hpp  (옵션, 메트릭, 파싱, 헬퍼)
  │    │    ├─ perf_stream_arg_reader.hpp (arg_reader_t)
  │    │    ├─ perf_stream_common.hpp     (프로토콜 유틸, 공유 상수)
  │    │    └─ stream_client.hpp          (전송 추상화)
  │    ├─ perf_stream_client_session.hpp  (세션, 비동기 상수, 래치)
  │    │    ├─ perf_stream_bench_client_iface.hpp  (인터페이스)
  │    │    └─ perf_stream_common.hpp
  │    └─ perf_stream_common.hpp
  └─ perf_stream_client_options.hpp
```

## 클래스 다이어그램

```
┌──────────────────────────────────────────────────────────────────┐
│  perf_stream_client.cpp                                          │
│                                                                  │
│  ┌─────────────────┐     ┌──────────────────────────────────┐    │
│  │ client_options_t │     │ bench_client_t                   │    │
│  │                  │────▶│   : bench_client_iface_t         │    │
│  │ transport        │     │                                  │    │
│  │ pattern          │     │ io_context + 워커 스레드         │    │
│  │ host, port       │     │ sessions: [client_session_t ×N]  │    │
│  │ ccu              │     │ connected_sessions               │    │
│  │ sizes[]          │     │ loopback_bind_plan               │    │
│  │ warmup, duration │     │ rtt_samples_bits (atomic 링버퍼) │    │
│  │ io_threads       │     │                                  │    │
│  │ ...              │     │ run()                            │    │
│  └─────────────────┘     │ schedule_connects()              │    │
│                           │ on_connect_result()              │    │
│                           │ allow_send()                     │    │
│                           │ on_recv_done()                   │    │
│                           │ run_case() → run_window()        │    │
│                           └──────────┬───────────────────────┘    │
│                                      │ N개 인스턴스 소유          │
│                           ┌──────────▼───────────────────────┐   │
│                           │ client_session_t                  │   │
│                           │   → bench_client_iface_t &owner   │   │
│                           │                                   │   │
│                           │ tcp::socket (raw)                 │   │
│                           │ strand (직렬화 디스패치)          │   │
│                           │                                   │   │
│                           │ begin_connect()                   │   │
│                           │   → do_connect() → on_connect()   │   │
│                           │   → 실패 시 타이머로 재시도       │   │
│                           │                                   │   │
│                           │ start_traffic()                   │   │
│                           │   → maybe_send_more()             │   │
│                           │   → send_one() [async_write]      │   │
│                           │   → on_write()                    │   │
│                           │   → start_read_header()           │   │
│                           │   → on_read_header()              │   │
│                           │   → start_read_payload()          │   │
│                           │   → on_read_payload()             │   │
│                           │   → maybe_send_more() (루프)      │   │
│                           └───────────────────────────────────┘   │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│  stream_client.hpp (헤더 전용)                                   │
│                                                                  │
│  ┌──────────────────────────────────────────────────┐            │
│  │ stream_client_t                                   │            │
│  │                                                   │            │
│  │ 전송 추상화 (동기 Boost.Asio).                    │            │
│  │ 지원: tcp, tls, ws, wss                           │            │
│  │ stop 토큰 전송에 사용.                            │            │
│  │                                                   │            │
│  │ connect()                                         │            │
│  │ send_payload(payload)   → [len32be 헤더]+데이터   │            │
│  │ recv_payload(out, size) ← [len32be 헤더]+데이터   │            │
│  │ close()                                           │            │
│  │                                                   │            │
│  │ 내부:                                             │            │
│  │   write_frame_bytes()                             │            │
│  │   read_frame_bytes()                              │            │
│  │     ├─ tcp/tls: read_exact_tcp_like() (4B + N)    │            │
│  │     └─ ws/wss:  read_ws_message_bytes()           │            │
│  │                 + ws_pending_frame 재조립          │            │
│  │                                                   │            │
│  │ 소켓 (모드별 하나만 활성):                        │            │
│  │   tcp_socket  → tcp::socket                       │            │
│  │   tls_socket  → ssl::stream<tcp::socket>          │            │
│  │   ws_socket   → beast::websocket::stream<tcp>     │            │
│  │   wss_socket  → beast::websocket::stream<ssl>     │            │
│  └──────────────────────────────────────────────────┘            │
└──────────────────────────────────────────────────────────────────┘
```

## 와이어 프로토콜

모든 transport에서 고정 `len32be` 프레이밍 사용:

```
┌──────────────────┬──────────────────────────────┐
│  4 바이트 (BE)   │  N 바이트                     │
│  페이로드 길이   │  페이로드 데이터              │
└──────────────────┴──────────────────────────────┘
```

- 모든 `send_payload()`는 `[4바이트 빅엔디안 길이][페이로드]`를 전송한다.
- 모든 `recv_payload()`는 동일한 프레이밍으로 수신한다.
- ws/wss: len32be 프레임이 WebSocket 바이너리 메시지 안에 패킹된다.
  하나의 WS 메시지에 여러 len32be 프레임이 도착할 수 있으므로,
  수신 측은 `ws_pending_frame`에 부분 데이터를 버퍼링하여 재조립한다.
- `--pattern`은 프레이밍 모드 전환이 아닌, 결과 레이블링/러너 라우팅 용도이다.
- 페이로드 크기 범위: 16 ~ 4 MiB (`k_stream_min_chunk_size` ~ `k_stream_max_chunk_size`).

## 벤치마크 생명주기

```
1. CLI 옵션 파싱
       │
2. io_context + N개 워커 스레드 생성 (--io-threads)
       │
3. CCU개의 client_session_t 인스턴스 생성
       │
4. 배치 연결 (k_connect_batch=1024씩)
   ┌─────────────────────────────────────────────┐
   │ do_connect() ──▶ async_connect()            │
   │   ├─ 성공: 보고, 소켓 튜닝 적용             │
   │   └─ 실패: 25ms 후 재시도                   │
   │       └─ 90초 타임아웃 후 포기               │
   └─────────────────────────────────────────────┘
       │
5. --sizes의 각 메시지 크기에 대해:
       │
   ├─ 5a. 연결된 모든 세션에 set_chunk_size() 호출
   │       (resize_latch_t 배리어 사용, 30초 타임아웃)
   │
   ├─ 5b. 워밍업 윈도우 (--warmup 초, 메트릭 수집 안 함)
   │       └─ run_window(warmup, measure=false)
   │
   ├─ 5c. 측정 윈도우 (--duration 초, 메트릭 수집)
   │       └─ run_window(duration, measure=true)
   │           ├─ kick_phase_for_connected()로 트래픽 시작
   │           ├─ 각 세션: send_one() → async_write
   │           │   → on_write() → start_read_header()
   │           │   → on_read_header() → start_read_payload()
   │           │   → on_read_payload() → maybe_send_more()
   │           └─ sleep(duration) 후 중지
   │
   ├─ 5d. 드레인 (--drain-ms, 진행 중인 작업 완료 대기)
   │
   ├─ 5e. 메트릭 수집 및 보고
   │       └─ 처리량, 레이턴시 백분위수 (p50/p95/p99)
   │
   └─ 5f. 크기 전환 드레인 (--size-transition-drain-ms)
       │
6. 모든 세션 종료, 워커 스레드 join
       │
7. (선택) 서버에 stop 토큰 전송
```

## 레이턴시 샘플링

`--latency-sample-rate N` 설정 시, 매 N번째 메시지의 페이로드 앞
16바이트에 타이밍 데이터를 삽입한다:

```
┌───────────────┬───────────────┬──────────────┐
│ 8 바이트 (BE) │ 8 바이트 (BE) │ 나머지       │
│ 시퀀스 번호   │ sent_ns       │ (무시)       │
└───────────────┴───────────────┴──────────────┘
```

에코 서버는 페이로드를 변경 없이 반사한다. 수신 시 클라이언트는
`seq`와 `sent_ns`를 읽어 RTT = `now_ns - sent_ns`를 계산하고,
atomic 링버퍼(`k_rtt_sample_capacity` = 1M 엔트리)에 샘플을 저장한다.
샘플은 `double → uint64_t` 비트 캐스팅으로 `std::atomic<uint64_t>[]`에
저장하여 I/O 스레드에서의 스레드 안전 쓰기와 메인 스레드에서의 안전한 읽기를 보장한다.

## 루프백 포트 샤딩

서버 엔드포인트가 루프백(127.x.x.x)이고 CCU가 OS의 임시 포트 범위를 초과하면,
클라이언트는 포트 고갈을 방지하기 위해 소스 주소를 여러 루프백 IP
(127.0.0.1, 127.0.0.2, ...)로 자동 분산한다. 필요한 샤드 수:

```
shards = ceil(ccu / 사용 가능한 임시 포트 수)
```

임시 포트 범위는 `/proc/sys/net/ipv4/ip_local_port_range`에서 읽는다.

## CLI 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--transport` | `tcp` | 전송 프로토콜: `tcp`, `tls`, `ws`, `wss` |
| `--pattern` | `STREAM` | 결과 출력 라우팅용 레이블 |
| `--endpoint` | — | 전체 엔드포인트 URI (예: `tcp://127.0.0.1:15557`). `--host`/`--port`/`--transport`를 덮어씀. |
| `--host` | `127.0.0.1` | 서버 호스트 |
| `--port` | `38001` | 서버 포트 |
| `--ccu` | `1000` | 동시 연결 수 |
| `--sizes` | `64,1024,65536` | 쉼표 구분 페이로드 크기 (바이트) |
| `--runs` | `1` | 크기별 벤치마크 반복 횟수 |
| `--warmup` | `2` | 워밍업 시간 (초) |
| `--duration` | `10` | 측정 시간 (초) |
| `--drain-ms` | `300` | 측정 후 드레인 대기 (ms) |
| `--size-transition-drain-ms` | `300` | 크기 전환 간 드레인 (ms) |
| `--io-threads` | `4` | I/O 워커 스레드 수 |
| `--latency-sample-rate` | `0` | 매 N번째 메시지 레이턴시 샘플링 (0=비활성) |
| `--print-perf-result` | `0` | 출력 형식: 0=상세, 1=양쪽, 2=CSV 전용 |
| `--send-stop-token` | `0` | 벤치마크 후 서버에 stop 토큰 전송 |
| `--stop-token` | `__zlink_perf_stop__` | 커스텀 stop 토큰 문자열 |

## 진입점

- 바이너리: `perf_stream_client`

## 빌드

로컬 빠른 빌드:

```bash
./core/perf/common/streamclient/build.sh
```

출력:

- `core/perf/common/streamclient/build/perf_stream_client`

전체 CMake 타겟:

```bash
cmake --build core/build --target perf_stream_client -j$(nproc)
```

## 실행 예시

```bash
core/build/bin/perf_stream_client \
  --pattern MULTI_STREAM \
  --transport tcp \
  --endpoint tcp://127.0.0.1:15557 \
  --sizes 64 \
  --ccu 1000 \
  --warmup 3 \
  --duration 5 \
  --io-threads 4 \
  --print-perf-result 2 \
  --send-stop-token 1
```

`--transport`는 `tcp,tls,ws,wss`를 지원한다.

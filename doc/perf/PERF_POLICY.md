# zlink Performance Test Policy (통합)

> **적용 범위**: zlink 전체 (core + bindings)
> **Policy Version**: 1.9
> **Date**: 2026-03-21
> **Scope**: zlink 성능 테스트 통합 정책 — 공통 구조, 통합 실행, 비교 스크립트
>
> 본 정책은 `perf/`의 C++ 벤치마크와 현재 perf suite가 구현된 바인딩(`bindings/cpp`, `bindings/dotnet`, `bindings/java`)에 동일하게 적용된다. `bindings/node`는 in-repo perf 자산이 존재하지만 아직 shared policy parity를 맞추는 정렬 대상이므로, 본 문서는 Node에도 현재 기준 계약으로 적용한다. `bindings/python`은 아직 perf suite가 구현되지 않았으므로 본 문서의 구조/운영 원칙만 향후 기준으로 삼는다.

---

## 1. 문서 구조

| 문서 | 설명 |
|------|------|
| **PERF_POLICY.md** (본 문서) | 공통 디렉터리 구조, 통합 실행, 비교 스크립트 |
| [PERF_SINGLE_TEST_POLICY.md](PERF_SINGLE_TEST_POLICY.md) | single-client 벤치마크 측정 기준, 패턴, phase, 환경 변수 |
| [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md) | multi-client 벤치마크 측정 기준, 패턴, phase, 환경 변수 |

- 측정 방법, 패턴별 상세, phase 제어, 환경 변수 등은 각 개별 정책 문서를 참조한다.
- 본 문서는 **공통 구조**와 **통합 실행 방법**만 기술한다.
- 각 바인딩의 실행 스크립트 경로는 `perf/single/` 및 `perf/multi/`에 위치한다.

## 1.1 공통 원칙

아래 원칙은 `core/perf`와 모든 bindings perf에 동일하게 적용한다.

- perf/bench 코드는 `doc/guide` 및 `doc/api` 문서에 기술된 public C API만
  사용한다. 내부 헤더나 내부 함수를 직접 호출하지 않는다.
- public C API 동작에 문제가 있으면 perf 코드에서 우회하지 않고 버그로
  레포팅한다. 버그레포팅 문서는 doc/bug/perf 아래에 md 파일 형식으로 작성한다. 버그는 회귀테스트를 작성해서 재현을 확인하고 수정한다. 버그 를 우선 후정하고 이어서 perf 작업을 계속한다.
- 측정 의미는 유지한다.
  - `ready / warmup / active`
  - `RESULT` 포맷
  - 실패 의미
- 실제 오류는 즉시 `fail` 처리한다.
- `EAGAIN`은 오류가 아니라 flow-control 상태로 취급한다.
- perf 측정용 I/O 경로는 두 가지 모델을 지원한다.
  - **recv 모델** (`--recv recv`):
    - recv: poller `POLLIN` readiness 감지 → 비동기 `zlink_recv()` /
      `zlink_msg_recv()` drain 루프 (react 방식).
    - send backpressure: poller `POLLOUT` readiness 감지 → writable 상태에서만
      send 수행.
    - poller가 recv/send 양쪽의 readiness 제어를 담당한다.
  - **callback 모델** (`--recv callback`):
    - callback 모델은 모든 패턴의 일반 옵션이 아니다.
    - single suite는 callback 모드만 기본 테스트 대상으로 둔다.
    - multi suite는 recv 모드만 기본 테스트 대상으로 둔다.
    - dual-mode 예외는 multi `SPOT` / `STREAM`만 허용한다.
    - `recv`와 `callback`은 같은 pattern 안에서 `--recv` 값으로 선택한다.
      callback 전용 파일명이나 별도 public pattern 이름을 정책에 추가하지
      않는다.
    - recv: pattern별 callback API 등록 → 라이브러리가 I/O thread에서 callback
      dispatch.
    - callback hot path는 메시지 수명/집계를 오래 붙들지 않고, 필요한 최소
      metric event만 추출해 bounded queue로 넘긴다.
    - throughput/latency/phase window 집계는 callback 밖 metric worker가
      수행한다. single callback은 전용 worker를 사용하고, multi callback은
      metric worker와 app thread가 역할을 분리한다.
    - send backpressure는 `zlink_send_ready_handler()`를 포함한 callback 모델의
      보조 메커니즘으로 사용하며, callback 모델의 본체는 `recv callback +
      bounded queue + metric worker`다.
    - 단, single callback은 전용 sender가 active 구간 동안 blocking send를
      수행하는 모델을 기본으로 하며, `zlink_send_ready_handler()`는 single
      callback의 필수 계약이 아니다. `send_ready_handler` 기반 backpressure는
      multi callback 또는 single의 별도 nonblocking variant가 있을 때만 적용한다.
    - poller는 사용하지 않는다.
- 실행 스크립트의 `--recv` 옵션으로 모델을 선택한다.
  - single 기본값: `callback`
  - multi 기본값: `recv`
- 지원하지 않는 pattern에서 `--recv callback`을 지정하면 policy violation으로
  즉시 실패해야 한다. silent fallback은 금지한다.
- 같은 측정 구간에서 두 모델의 recv/send 메커니즘을 섞지 않는다.
- 단, 두 모델은 동일한 metric header decode, phase 판정, throughput/latency
  집계 엔진을 공유하는 방향으로 구현해야 한다. 모델 차이는 event source
  (`recv` drain vs callback dispatch) 에서만 남겨야 한다.
- setup/handshake 단계의 bounded validation 1회는 허용하되, 측정 구간으로
  들어가기 전에 종료되어야 한다.
- hot loop 안에서는 아래를 금지한다.
  - retry budget
  - sleep / yield
  - fallback
  - cap
  - heap alloc
  - 문자열 생성
  - 로그 출력
  - 불필요 복사
- send/recv 버퍼는 루프 밖에서 1회 할당하고 재사용한다.
- 핵심 send/recv loop는 각 패턴 파일 안에서 명시적으로 보여야 한다.
- registry summary/topology query는 global/coarse 상태 확인용으로만 사용한다.
- registry summary는 eventually consistent view이므로 benchmark의 final strict
  start gate로 사용하지 않는다.
- perf 연결 준비/handshake는 pattern별로 나눈다.
  - 일반 raw 패턴: low-cost monitor event `CONNECTION_READY`
  - SPOT: explicit `READY/START` barrier protocol
- raw perf ready gate는 expected client 수만큼 `CONNECTION_READY` 수신으로
  판정한다.
- SPOT perf ready gate는 monitor event 나 snapshot 이 아니라 benchmark control
  protocol 로 판정한다.
- multi SPOT barrier 의 `READY` 는 `connect_peer()` 직후 즉시 보내지 않는다.
  local benchmark network 정책으로, 각 client spot 이 connect setup 을 끝낸 뒤
  고정 stabilization window(기본 1초)를 거쳐 server spot 으로 `READY` 를
  전송한다. server 는 expected client 수만큼 `READY` 를 받은 뒤 `START` 를
  broadcast 한다.
- 위 stabilization window 는 SPOT perf barrier 의 일부이며, raw pattern 의
  monitor ready gate 와 동일한 public 계약으로 취급하지 않는다.
- `setup_connected_pair()` 같은 helper는 raw pattern 의 `CONNECTION_READY`
  counting 만 캡슐화한 경우에만 허용된다.
- `wait_ready()` 같은 helper는 허용한다. 단:
  - raw pattern 에서는 `CONNECTION_READY` counting 만 수행해야 한다.
  - SPOT 에서는 explicit `READY/START` barrier 만 수행해야 한다.
  - delivery-ready event, service monitor, snapshot polling 을 helper 뒤에
    숨기면 안 된다.
- suite별 정책 문서는 pattern별 low-cost ready gate event를 명시해야 한다.
  perf는 그 표에 없는 추가 precondition(`FILTER_APPLIED`, delivery-ready exact count,
  quorum 완화, 보정용 handshake 단계)을 두지 않는다.
- perf start gate 구현에서 아래를 금지한다.
  - `sleep`/`msleep`/고정 지연
  - monitor snapshot polling
  - ad-hoc retry loop
- perf lifecycle에서 아래와 같은 **벤치 단계**를 새로 만들지 않는다.
  - `preflight`
  - `prime`
  - `settle`
  - `stable`
  - `quiet`
  - `quiescent`
  - `idle drain`
- 위 단계가 이미 존재하지만 실제로는 “ready 이벤트 하나 기다리기” 또는
  “phase 종료 후 남은 메시지 정리”를 우회적으로 표현한 것뿐이라면, 새 단계로
  유지하지 말고 삭제하거나 기존 `ready -> warmup -> active` 흐름에
  흡수한다.
- raw pattern 의 ready gate event 는
  [`doc/guide/06-monitoring.ko.md`](../guide/06-monitoring.ko.md)의
  raw socket monitoring 절을 단일 기준으로 따른다.
- SPOT 은 service monitor 를 사용하지 않으며, perf-ready 는 barrier protocol 로만
  정의한다.
- monitor event rename:
  - raw socket ready event 는 `CONNECTION_READY` 이다.
- routing 검증이 필요한 패턴(예: ROUTER)은 monitor-ready 이후
  단발성 self-check 1회만 수행하고, 실패 시 즉시 fail 처리한다.
- registry/bootstrap/query/summary 조회는 measurement phase 밖에서만 수행한다.

## 1.2 Binary And Runner Responsibilities

perf 구조는 다음 두 책임으로 분리한다. 이 분리는 `core/perf`와 bindings perf에
동일하게 적용한다.

### 1.2.1 바이너리 책임

- 바이너리는 **단일 측정 케이스**만 수행한다.
- 단일 측정 케이스의 최소 단위는 `pattern/transport/size/run` 이다.
- 바이너리는 입력 조건에 따라 해당 케이스의 ready/warmup/active를 수행하고,
  `RESULT,...` line과 필요한 제어 신호만 stdout으로 출력한다.
- 바이너리는 다음 책임을 가지지 않는다.
  - 여러 pattern 순회
  - 여러 transport 순회
  - 여러 size 반복 실행 orchestration
  - runs > 1 집계
  - markdown table 포맷팅
  - median/최종 report 저장
- 바이너리 내부에서 측정 hot path를 흐리게 하는 report formatting, 문자열 조합,
  동적 집계 컨테이너 orchestration 로직을 추가하면 안 된다.
- callback 기반 측정에서 callback dispatch thread는 phase별 count 증가와
  최소 metric event enqueue까지만 수행한다. throughput/latency 계산,
  sample aggregation, 완료 대기는 callback 밖 worker에서 처리해야 하며,
  패턴별 예외를 두지 않는다.

### 1.2.2 Runner 책임

- runner(`run_benchmarks*.sh/.ps1`, `run_comparison.py`)는 전체 suite orchestration을 담당한다.
- runner 책임:
  - pattern/transport/size/run 순회
  - 프로세스 시작/종료 및 READY 대기
  - cooldown 적용
  - RESULT line 수집/파싱
  - runs > 1 median 집계
  - markdown table 출력
  - 결과 파일 저장 및 complete/partial 판정
- 사람용 출력 형식과 결과 파일 구조는 runner에서만 관리한다.
- policy 변경으로 출력 형식이나 완료 판정 로직이 바뀌면, 바이너리가 아니라
  runner를 우선 수정한다.

### 1.2.3 구조 불변식

- `pattern/transport/size/run` 단위 측정 의미를 바이너리 밖 runner가 조합한다.
- 바이너리는 “한 케이스 실행 + RESULT line 출력”을 넘는 orchestration 책임을
  가져서는 안 된다.
- runner 리팩토링은 이 책임 분리를 유지해야 하며, 관련 자동 검증(test)도 함께
  갱신해야 한다.

## 1.3 패턴 해석 규칙

- echo
  - request/reply 의미를 유지한다.
  - send 역할과 recv 역할 정책을 둘 다 적용한다.
- one-way send
  - recv 정책은 없다.
  - send 정책만 적용한다.
- one-way recv
  - send 정책은 없다.
  - recv 정책만 적용한다.
- `PUBSUB`, `SPOT`
  - publisher/server는 one-way send다.
  - subscriber/client는 one-way recv다.

---

## 2. 디렉터리 구조

### 2.0 core (CAPI 레퍼런스 구현)

> 아래 경로는 `core/perf/` 기준이다. 정책 문서 자체는 `doc/perf/`에 위치한다.

```text
core/perf/
├── run_benchmarks.sh / .ps1                # single 전용 실행 스크립트 (Linux/Windows)
├── run_benchmarks_multi.sh / .ps1          # multi 전용 실행 스크립트 (Linux/Windows)
├── run_comparison.py                       # 통합 비교/실행 스크립트
├── single/                                 # single 벤치마크 소스
├── multi/                                  # multi 벤치마크 소스
└── results/                                # 결과 저장 루트
    ├── single/
    │   └── report/                         # 결과 레포트
    └── multi/
        └── report/                         # 결과 레포트
```

### 2.0.1 bindings (바인딩 라이브러리)

각 바인딩은 동일한 `perf/` 하위 구조를 따른다.

```text
perf/                                       # bindings/<lang>/perf/
├── single/
│   ├── run_benchmarks.sh / .ps1            # single 전용 실행 스크립트
│   └── ...                                 # 언어별 벤치마크 소스
├── multi/
│   ├── run_benchmarks.sh / .ps1            # multi 전용 실행 스크립트
│   └── ...
└── results/                                # 결과 저장 루트 (core와 동일 구조)
    ├── single/
    │   └── report/
    └── multi/
        └── report/
```

### 2.0.2 소스 파일 위치 및 명명 규칙

#### 소스 위치 규칙

- 모든 벤치마크 소스는 해당 suite의 디렉터리에 위치해야 한다:
  - single: `perf/single/`
  - multi: `perf/multi/`
- 언어별 빌드 시스템이 하위 프로젝트를 요구하는 경우 `perf/single/` 또는 `perf/multi/` 안에 프로젝트 디렉터리를 둔다 (예: .NET의 `Zlink.BindingBench/`, Java의 Gradle/Maven 모듈).
- 벤치마크 소스를 언어 메인 프로젝트의 테스트 트리(예: `src/test/`)에 두지 않는다.

#### 명명 접두어 규칙

- 모든 벤치마크 소스 파일은 **`perf_`** 접두어를 사용한다 (PascalCase 언어는 `Perf` 접두어).
- 기본 패턴: `perf_<pattern>` — 각 언어의 명명 컨벤션을 적용한다.
- `recv`와 `callback`은 같은 pattern 안에서 `--recv` 값으로 선택한다.
- callback 모드를 이유로 별도 callback 파일명이나 별도 public pattern 이름을
  정책에 추가하지 않는다.
- **예외**: 공통 유틸리티 헤더는 `perf_` 접두어 없이 명명할 수 있다 (예: `bench_common.hpp`, `perf_common.hpp`).
- 상세 파일명 규칙은 개별 정책 문서를 참조한다:
  - Single: [PERF_SINGLE_TEST_POLICY.md § 10.1](PERF_SINGLE_TEST_POLICY.md)
  - Multi: [PERF_MULTI_TEST_POLICY.md § 11.1](PERF_MULTI_TEST_POLICY.md)

#### 소스 위치 테이블

| 언어 | single 소스 위치 | multi 소스 위치 | 공통 유틸리티 |
|------|-----------------|----------------|-------------|
| Core (C++) | `perf/single/src/` | `perf/multi/src/` | `perf/single/common/`, `perf/multi/common/` |
| C++ binding | `perf/single/` | `perf/multi/` | `perf_dispatch.hpp` |
| .NET | `perf/single/Zlink.BindingBench/` | `perf/multi/<project>/` 또는 `perf/single/Zlink.BindingBench/` 내 multi role entrypoint | `PerfCommon.cs` |
| Java | `perf/single/<project>/` | `perf/multi/<project>/` 또는 `perf/single/<project>/` 내 multi role entrypoint | `PerfUtil.java` |
| Node | `perf/single/` | `perf/multi/` | `perf/common/` |
| Python | 미구현 | 미구현 | - |

- 컴파일 언어 바인딩(C++/.NET/Java)은 소스 트리 분리 대신 단일 runner에서 `--multi-server`/`--multi-client` role 분기를 제공해도 된다. 이 경우에도 결과 형식, 운영 모드, server/client 프로세스 모델은 동일하게 준수해야 한다.

### 2.0.3 STREAM 소켓 테스트 모델 (공통 필수)

- **STREAM 계열은 multi suite에서만 테스트한다.** single suite에서는 STREAM 소켓 테스트를 수행하지 않는다.
- STREAM 계열(`MULTI_STREAM`)은 반드시 **zlink STREAM server(bind only)** +
  **raw transport client(connect)** 모델로 측정한다.
- zlink STREAM 소켓의 client `connect()` 경로를 벤치마크 클라이언트로 사용하지 않는다.
- STREAM 테스트에서 server를 DEALER/ROUTER/PUBSUB 등 non-STREAM 소켓으로 대체하면 정책 위반이며 결과는 무효다.
- 모델 위반/불일치 구현은 정책 위반으로 간주하며, 해당 코드 경로를 삭제한 뒤 정책 모델로 재구현해야 한다.
- 모델 위반 구현에서 나온 결과는 `UNSUPPORTED`/`SKIP`으로 우회할 수 없으며 정책 산출물로 인정하지 않는다.
- STREAM multi 측정에서는 각 size마다 `connect_ok == target clients`(100%)를 충족해야 하며, 미달 시 반드시 `fail`로 처리한다.

#### Wire Protocol

STREAM 계열 벤치마크는 **len32be framing** 프로토콜로 통일한다.

```text
┌──────────────────────┬──────────────────────────────┐
│  4 bytes (big-endian) │         payload              │
│   payload length      │    (length bytes)            │
└──────────────────────┴──────────────────────────────┘
```

- **client**: 모든 STREAM 패턴에서 동일한 공통 raw client를 사용하며, `[4B length (big-endian)][payload]` 형식으로 송신한다. 수신(echo)도 동일한 framing으로 읽는다.
- **server**: zlink STREAM 소켓으로 bind한 뒤, 수신 방식에 따라 2가지 패턴으로 분기한다:

| 패턴 | `--recv` | server 수신 방식 | 설명 |
|------|----------|-----------------|------|
| STREAM / MULTI_STREAM | `recv` | 기본 recv 루프 | 기존 소켓 recv API로 메시지 수신 |
| STREAM / MULTI_STREAM | `callback` | callback dispatch | stream dispatch callback API로 수신 |

- client의 wire protocol을 len32be로 통일하는 이유: 서버 수신 방식만 다르고 client는 동일한 공통 바이너리를 사용하므로, 테스트 용이성과 비교 공정성을 위해 client 측 framing을 len32be로 고정한다.
- 이 프로토콜은 multi suite에 적용된다. single suite에서는 STREAM 테스트를 수행하지 않는다.
- legacy callback-named / len32be-named STREAM 패턴은 삭제 대상이다. public
  policy surface에서는 `STREAM` / `MULTI_STREAM` + `--recv recv|callback`
  조합만 사용한다.

### 2.1 결과 저장 규칙

| 항목 | 규칙 |
|------|------|
| 파일명 형식 | `perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt` |
| 날짜 디렉터리 | 사용하지 않음 (파일명에 날짜/시간 포함) |
| `<platform>` | `linux`, `windows`, `macos` |
| `<recv_mode>` | `recv`, `callback` |
| `<tag>` | `--results-tag` 옵션으로 지정 (선택) |

### 2.2 보존 정책

#### 파일 수 기반 정리

| 디렉터리 | 최대 파일 수 | 초과 시 처리 |
|-----------|-------------|-------------|
| `report/` | 기본 100 | 파일명 사전순 기준 오래된 파일 삭제 |

- `single/`과 `multi/` 각각 독립적으로 적용한다.
- single 엔진은 최대 파일 수를 100으로 하드코딩한다 (`PERF_RESULTS_MAX_FILES` 미참조).
- multi 엔진은 `PERF_RESULTS_MAX_FILES` 환경 변수를 읽는다 (기본 100).

#### 시간 기반 정리 (core 전용)

| 환경 변수 | 기본값 | 동작 |
|-----------|--------|------|
| `PERF_RESULTS_RETENTION_DAYS` | 90 | 결과 디렉터리 중 `YYYYMMDD` 형식 이름이 기준일보다 오래된 디렉터리를 삭제 |

- `core/perf/run_benchmarks.sh` 실행 시 자동 적용된다.
- bindings 스크립트에는 적용되지 않는다.

### 2.3 저장 단위

- 스크립트 1회 실행 = 1개 결과 파일. 실행에서 측정된 모든 패턴/transport/size 조합의 결과가 하나의 파일에 기록된다.
  - **예외**: multi에서 preflight(nofile/memory) 검사로 모든 패턴이 skip되면 결과 파일 없이 `exit 0`한다.

---

## 3. 실행 스크립트

### 3.1 개별 실행

| suite | core 스크립트 | bindings 스크립트 | 정책 문서 |
|-------|--------------|-------------------|-----------|
| single | `core/perf/run_benchmarks.sh` / `.ps1` | `bindings/perf/run_policy_bench.py --suite single --binding <binding>` | [PERF_SINGLE_TEST_POLICY.md](PERF_SINGLE_TEST_POLICY.md) |
| multi | `core/perf/run_benchmarks_multi.sh` / `.ps1` | `bindings/perf/run_policy_bench.py --suite multi --binding <binding>` | [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md) |

```bash
# core single만 실행
core/perf/run_benchmarks.sh --pattern PAIR

# core multi만 실행
core/perf/run_benchmarks_multi.sh --pattern MULTI_STREAM

# bindings 실행 (예: cpp)
bindings/perf/run_policy_bench.py --binding cpp --suite single --pattern PAIR
bindings/perf/run_policy_bench.py --binding cpp --suite multi --pattern MULTI_STREAM
```

각 스크립트의 상세 옵션은 개별 정책 문서의 섹션 5를 참조한다.

> **정책 준수 실행기**: 아래 스크립트가 각 suite의 유일한 정책 준수 실행기이다. core single은 `single/run_comparison.py`를, core multi는 `run_comparison.py`를 내부 실행/비교 엔진으로 사용한다 (`run_benchmarks.sh`에서 `PERF_ALLOW_MULTI=1` 여부로 경로를 결정한다). bindings는 `bindings/perf/run_policy_bench.py`를 내부 실행/비교 엔진으로 사용한다.
>
> | suite | core | bindings |
> |-------|------|----------|
> | single | `core/perf/run_benchmarks.sh` | `bindings/perf/run_policy_bench.py --suite single --binding <binding>` |
> | multi | `core/perf/run_benchmarks_multi.sh` | `bindings/perf/run_policy_bench.py --suite multi --binding <binding>` |
>
> core는 single/multi 스크립트가 같은 디렉터리에 있으므로 `_multi` 접미어로 구분한다. bindings는 중앙 정책 실행기 `bindings/perf/run_policy_bench.py`를 사용한다.

### 3.2 통합 실행

실행 엔트리포인트는 wrapper 스크립트다 (§ 3.1 정책 준수 실행기 테이블 참조).
- core single: `run_benchmarks.sh` / `.ps1`
- core multi: `run_benchmarks_multi.sh` / `.ps1`
- bindings: `bindings/perf/run_policy_bench.py --binding <binding> --suite <single|multi>`
- 단일 실행에서 single/multi를 혼합하지 않는다.

```bash
# core: single만 실행
core/perf/run_benchmarks.sh --pattern ALL

# core: multi만 실행
core/perf/run_benchmarks_multi.sh --pattern ALL

# core: 특정 패턴만
core/perf/run_benchmarks.sh --pattern PAIR,PUBSUB

# core: single 기본 모드(callback)로 실행
core/perf/run_benchmarks.sh --pattern PAIR

# core: 태그 추가
core/perf/run_benchmarks.sh --results-tag v1.5.0

# bindings: 동일한 옵션 체계 적용 (예: java)
bindings/perf/run_policy_bench.py --binding java --suite single --pattern ALL
bindings/perf/run_policy_bench.py --binding java --suite multi --pattern ALL
```

### 3.3 통합 실행 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 측정할 패턴 (쉼표 구분) | 전체 |
| `--runs N` | 반복 횟수 | 1 (Windows multi `.ps1`: 3) |
| `--build-dir PATH` | 빌드 디렉터리 경로 | 자동 탐색 |
| `--reuse-build` | 기존 빌드 재사용 (configure/build 생략) | off |
| `--clean-build` | 빌드 디렉터리 삭제 후 클린 빌드 | off (기본은 증분 빌드) |
| `--pin-cpu` | CPU pinning (Linux: taskset, Windows: processor affinity) | off |
| `--output PATH` | stdout tee 출력 | stdout만 |
| `--results-dir PATH` | 결과 저장 루트 override | `perf/results` |
| `--results-tag NAME` | 결과 파일명 태그 | 없음 |
| `--msg-sizes LIST` | 메시지 크기 목록 | suite별 기본값 |
| `--transports LIST` | transport 목록 | suite별 기본값 |
| `--recv MODE` | recv 모델 선택. single은 `callback`만 허용, multi 기본값은 `recv`. dual-mode 예외는 multi=`SPOT`/`STREAM`만 허용 | suite별 기본값 |

- `--recv` 옵션은 측정 구간의 수신 경로를 결정한다. `recv`는 동기 recv +
  poller 기반, `callback`은 recv handler callback 기반이다.
- single은 callback only다.
- multi는 recv only를 기본으로 하고 `SPOT`/`STREAM`만 `recv`/`callback`
  예외를 둔다.
- monitor 관련 검증은 suite와 무관하게 callback 기준으로만 수행한다.
- suite별 고유 옵션(`--clients` 등)은 개별 스크립트 호출 시 전달한다.

---

## 4. 결과 파일 형식 (공통)

### 4.1 파일 구조

결과는 `report/`에 사람이 읽을 수 있는 형식으로 저장한다.

#### report/ (결과 레포트)

```text
## Effective Options (start)
- runs: 1
- patterns: PAIR, SPOT
- transports: tcp, tls, ws, wss
- msg_sizes: 64, 256, 1024, 65536, 131072, 262144
- recv_mode: recv
- pin_cpu: off

===============================================================================

## PATTERN: PAIR (one-way)

### Transport: tcp
| Size     |       Throughput | Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | CPU% | Mem MB |
|----------|------------------|-----------|--------------|--------------|--------------|------|--------|
| 64B      |   523.40 Kmsg/s  | 33.5 MB/s |   12.35 us   |   18.20 us   |   21.40 us   | 48.2 |   12.3 |
| 1024B    |   120.30 Kmsg/s  | 123.2 MB/s|   52.10 us   |   70.55 us   |   92.10 us   | 52.1 |   14.1 |
```

- **실행 옵션 헤더 + TABLE**을 저장한다.
- `## Effective Options (start)` / `## Effective Options (result)` 섹션은 실행 시 사용된 옵션을 불릿 목록으로 출력한다. report/ 파일과 stdout 모두에 포함해야 한다.
- `recv_mode` 항목은 필수이며, 실제 실행에 사용된 `--recv` 값(`recv` 또는 `callback`)을 그대로 기록해야 한다.

### 4.2 RESULT line 형식

```text
RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>
```

| metric | 설명 | 필수 |
|--------|------|------|
| `throughput` | echo 패턴: 왕복 완료 수 (`ops/s`), one-way 패턴: 단방향 수신 수 (`msg/s`) | MUST |
| `bandwidth` | 네트워크 전송량 (MB/s) — multi echo: `throughput × size × 2 / 1,000,000`, 그 외(single 전체 + multi one-way): `throughput × size / 1,000,000` | MUST |
| `latency` | 레이턴시 (us) | MUST |
| `latency_p95` | 레이턴시 95th percentile (us) | MUST |
| `latency_p99` | 레이턴시 99th percentile (us) | MUST |
| `cpu_pct` | 프로세스 CPU 사용률 (%) — single용 | Linux/Windows |
| `mem_mb` | 프로세스 메모리 (MB, RSS/WorkingSet 기준) — single용 | Linux/Windows |
| `client_cpu_pct` | client 프로세스 CPU (%) — multi용 (바이너리 출력만, 최종 집계 테이블에는 미반영) | Linux/Windows |
| `client_mem_mb` | client 프로세스 메모리 (MB) — multi용 (바이너리 출력만, 최종 집계 테이블에는 미반영) | Linux/Windows |
| `server_cpu_pct` | server 프로세스 CPU (%) — multi용 | Linux/Windows |
| `server_mem_mb` | server 프로세스 메모리 (MB) — multi용 | Linux/Windows |
| `snd_pending_max` | 송신 큐 최대 대기 수 — single용 | informational |
| `rcv_pending_max` | 수신 큐 최대 대기 수 — single용 | informational |
| `rcv_pending_end` | 수신 큐 종료 시점 대기 수 — single용 | informational |
| `server_snd_pending_max` | server 송신 큐 최대 대기 수 — multi용 | informational |
| `server_rcv_pending_max` | server 수신 큐 최대 대기 수 — multi용 | informational |
| `server_rcv_pending_end` | server 수신 큐 종료 시점 대기 수 — multi용 | informational |

- throughput 단위는 패턴의 메시지 흐름 방향에 따라 결정된다. echo(왕복) 패턴은 `ops/s`, one-way(단방향) 패턴은 `msg/s`. 상세 분류는 개별 정책 문서 섹션 8.1을 참조한다.
- bandwidth는 throughput 단위가 다른 패턴 간에도 실제 데이터 처리량으로 직접 비교할 수 있는 공통 지표이다. 상세 계산은 개별 정책 문서 섹션 8.3을 참조한다.
- `cpu_pct`, `mem_mb`는 정보성(informational) 메트릭이다. 누락 시 완료 판정에 영향 없음.
- 상세 META 키 및 패턴별 측정 방식은 개별 정책 문서를 참조한다.

### 4.3 저장 규칙

결과는 항상 `<suite>/report/`에 저장된다 (complete/partial 무관).

- 파일명 형식: `perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt`
- `<recv_mode>`는 실제 실행에 사용된 `--recv` 값이며 `recv` 또는 `callback`이다.
- 완료 판정 기준: `expected == actual` (throughput + bandwidth + latency + latency_p95 + latency_p99 RESULT line 기준, 조합당 5줄).

---

## 5. 출력 형식 (공통)

### 5.1 스크립트 결과 테이블

> **구현 필수**: 모든 실행 스크립트는 RESULT line 외에 아래 형식의 사람이 읽을 수 있는 테이블을 **반드시 stdout에 출력하고, 결과 파일에도 TABLE 영역으로 기록**해야 한다. RESULT line만 출력하고 테이블을 생략하면 안 된다.

```text
## Effective Options (start)
- runs: 1
- patterns: PAIR, SPOT
- transports: tcp, tls, ws, wss
- msg_sizes: 64, 256, 1024, 65536, 131072, 262144
- recv_mode: recv
- pin_cpu: off

===============================================================================

## PATTERN: PAIR (one-way)

### Transport: tcp
| Size     |       Throughput | Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | CPU% | Mem MB |
|----------|------------------|-----------|--------------|--------------|--------------|------|--------|
| 64B      |   523.40 Kmsg/s  | 33.5 MB/s |   12.35 us   |   18.20 us   |   21.40 us   | 48.2 |   12.3 |
| 1024B    |   312.50 Kmsg/s  | 320.0 MB/s|   18.44 us   |   27.55 us   |   33.10 us   | 52.1 |   14.1 |


===============================================================================

## PATTERN: MULTI_DEALER_DEALER (one-way)

### Transport: tcp
| Size     |       Throughput |  Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
|----------|------------------|------------|---------------|---------------|---------------|--------|----------|
| 64B      |   150.00 Kmsg/s  |   9.6 MB/s |      0.05 ms  |      0.06 ms  |      0.08 ms  | 35.1   |   64.2   |
| 1024B    |   120.30 Kmsg/s  | 123.2 MB/s |      0.05 ms  |      0.07 ms  |      0.09 ms  | 38.5   |   66.8   |
```

- **패턴 간 구분선**: 패턴이 바뀔 때 `===============================================================================` 구분선을 출력한다 (첫 번째 패턴 앞에는 출력하지 않음).

| 컬럼 | 단위 | 비고 |
|------|------|------|
| Throughput | echo: `Kops/s`, one-way: `Kmsg/s` | 패턴 방향별 단위 — 개별 정책 문서 섹션 8.1 참조 |
| Bandwidth | `MB/s` | 네트워크 전송량 — 개별 정책 문서 섹션 8.3 참조 |
| Lat.Mean / Lat.P95 / Lat.P99 | single: `us` (마이크로초), multi: `ms` (밀리초) | 평균/95th/99th |
| CPU% | `%` | single 리소스 메트릭, 수집 실패 시 `N/A` |
| Mem MB | `MB` | single 리소스 메트릭, 수집 실패 시 `N/A` |
| S.CPU% / S.Mem MB | `%` / `MB` | multi server 리소스 메트릭 |

### 5.2 진행 로그

벤치마크 실행 중 **사이즈별 결과 테이블 행을 즉시 출력**하여 진행 상황과 측정 데이터를 동시에 제공한다.

#### 출력 규칙

| 항목 | 규칙 |
|------|------|
| 테이블 header | transport당 1회 출력 (header + separator) |
| 결과 행 | 사이즈별 결과 확정 즉시 출력 |
| `runs=1` | `run N/M:` 및 `median:` 레이블 없이 테이블만 출력 |
| `runs>1` | `run N/M:` 레이블 + 각 run 테이블 + `median:` 최종 테이블 |
| median 테이블 | 모든 run 완료 후 metric별 median 값으로 구성 |
| 실패 행 | metric 컬럼에 `FAIL` 표시 |
| 미지원 행 | metric 컬럼에 `UNSUPPORTED` 표시 |
| cooldown 표시 | `[cooldown Nms]`, `[transport cooldown Nms]` (multi) |
| 실패 표시 | `(failures=N) Done` |
| 조건 | 항상 출력 (`PERF_DEBUG`와 무관) |

- 컬럼 순서 및 형식은 § 5.1 결과 테이블과 동일하다.
- 바이너리 stderr는 stdout 결과에 통합하지 않지만, multi 엔진(`run_comparison.py`)은 stderr에서 `protocol not supported` 문자열을 감지하여 `unsupported` 자동 분류에 활용한다 (§ 8.4 참조).

상세 형식은 suite별 정책 문서를 참조한다:
- Single: [PERF_SINGLE_TEST_POLICY.md § 6.3](PERF_SINGLE_TEST_POLICY.md)
- Multi: [PERF_MULTI_TEST_POLICY.md § 6.3](PERF_MULTI_TEST_POLICY.md)

**Single (runs=1):**
```text
  > Benchmarking current for PAIR...
    Testing tcp:
      | Size     |       Throughput |  Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | CPU% | Mem MB |
      |----------|------------------|------------|--------------|--------------|--------------|------|--------|
      | 64B      |   523.40 Kmsg/s  | 33.5 MB/s  |   12.35 us   |   18.20 us   |   21.40 us   | 48.2 |   12.3 |
      | 256B     |   480.12 Kmsg/s  | 122.9 MB/s |   14.20 us   |   20.30 us   |   24.10 us   | 49.8 |   12.5 |
    Testing tcp: Done
```

**Multi (runs=3):**
```text
  > Benchmarking current for MULTI_DEALER_DEALER...
    Testing tcp | 64B,256B:
      run 1/3:
        | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|
        | 64B      |    121.98 Kmsg/s |    15.61 MB/s |      0.81 ms  |      1.01 ms  |      1.26 ms  |    N/A |      N/A |
        | 256B     |    ...
      [cooldown 3000ms]
      run 2/3:
        ...
      [cooldown 3000ms]
      run 3/3:
        ...
      median:
        | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|
        | 64B      |    ...
        | 256B     |    ...
    Testing tcp: Done
```

### 5.3 실패 요약

실패가 있는 경우 마지막에 요약이 출력된다.

```text
## Failures
- PAIR current ipc 64B: timeout
- MULTI_STREAM current wss 65536B: no_data
```

---

---

## 7. 리소스 메트릭 수집

| 메트릭 | Linux 수집 소스 | Windows 수집 소스 | 계산 방식 |
|--------|----------------|-------------------|-----------|
| `cpu_pct` | `/proc/[pid]/stat` | `GetProcessTimes()` | `(user₂+kernel₂ - user₁-kernel₁) / (elapsed × nproc) × 100` |
| `mem_mb` | `/proc/[pid]/status` VmRSS | `GetProcessMemoryInfo()` WorkingSetSize | 측정 종료 시점 1회 읽기, MB 변환 |

- 측정 phase 시작/종료 시점에 2회 샘플링.
- Single: 벤치마크 프로세스 대상. Multi: server/client 별도 프로세스별 독립 측정 (`client_cpu_pct`, `client_mem_mb`, `server_cpu_pct`, `server_mem_mb`).
- 정보성(informational) 메트릭이므로 누락 시 완료 판정에 영향 없음.

---

## 8. 실패 처리 정책 (공통 필수)

### 8.1 Retry(재시도) 금지

벤치마크 실행 스크립트 및 바이너리에 **retry/재시도 로직을 구현하지 않는다**.

| 항목 | 규칙 |
|------|------|
| 스크립트 레벨 재시도 | 금지 — 실패한 pattern/transport/size 조합을 자동으로 다시 실행하지 않는다 |
| 바이너리 내부 재시도 | 금지 — send/recv 실패 시 자동 재시도하지 않는다. `EAGAIN`은 pending 상태로 기록하고 이후 `PollOut` readiness에서 재개할 수 있으나, 동일 호출 흐름에서의 즉시 retry loop는 금지한다 |
| 환경 변수 | `PERF_MULTI_ATTEMPTS`, `PERF_MULTI_STREAM_ATTEMPTS` 및 레거시 `PERF_MULTI_ATTEMPTS`, `PERF_MULTI_STREAM_ATTEMPTS`는 **삭제 대상**이다. 구현에 존재하면 제거해야 한다 |

- **이유**: 재시도는 실패 원인을 숨긴다. 벤치마크 실패는 라이브러리 또는 환경의 실제 문제를 반영하며, 재시도로 통과시키면 회귀가 감지되지 않는다.

### 8.2 Inflight/Outstanding 옵션 금지

벤치마크 바이너리 및 스크립트에 **inflight, outstanding, in-flight 제한 옵션**을 두지 않는다.

| 항목 | 규칙 |
|------|------|
| CLI 옵션 | `--inflight`, `--outstanding`, `--max-in-flight` 등 inflight 깊이를 조절하는 옵션을 제공하지 않는다 |
| 환경 변수 | `PERF_INFLIGHT`, `PERF_MULTI_INFLIGHT`, `PERF_OUTSTANDING` 등 inflight 관련 환경 변수는 **삭제 대상**이다. 구현에 존재하면 제거해야 한다 |
| 하드코딩 flow control | `outstanding_limit`, `window_exhausted` 등 send/recv 차이 기반의 인위적 흐름 제어는 제거한다. 소켓 HWM(`PERF_MULTI_HWM`)이 이미 send 큐 backpressure를 제공한다 |

- **이유**: inflight 제한은 벤치마크 결과를 인위적으로 왜곡한다. 라이브러리의 실제 처리 능력을 측정해야 하며, 벤치마크 인프라가 추가 병목을 도입하면 안 된다.
- one-way 패턴에서는 응답이 없으므로 outstanding 개념 자체가 성립하지 않는다.
- echo 패턴에서는 클라이언트 측 per-socket pending 제어(1:1 send-recv)와 소켓 HWM이 자연 backpressure를 제공하므로 별도의 outstanding 제한이 불필요하다.

### 8.3 실패 시 대응 절차

| 단계 | 행동 |
|------|------|
| 1. 실패 기록 | 실패한 조합을 `## Failures` 섹션에 기록하고 결과 파일에 `status=partial`로 저장한다 |
| 2. 원인 파악 | 로그, 종료 코드, timeout 여부를 확인하여 실패 원인을 빠르게 파악한다 |
| 3-a. 벤치마크 코드 이슈 | 벤치마크 구현 버그이면 수정 후 재실행한다 |
| 3-b. core zlink 라이브러리 이슈 | 라이브러리 자체 결함이면 벤치마크 코드에서 우회(workaround)하지 않는다. 재현 가능한 회귀 테스트를 먼저 추가하고, core 버그를 수정한 뒤 perf 작업을 계속 진행한다 |
| 3-c. 환경 이슈 | OS 리소스(fd limit, port 고갈 등)이면 환경을 수정한 뒤 재실행한다 |

- 재시도로 문제를 숨기지 않는다. 실패는 반드시 원인을 파악한 뒤 근본 원인을 해결해야 한다.
- core 라이브러리 버그를 발견하면 "perf만 통과시키는 우회"를 금지한다. 반드시
  재현 테스트를 추가해 버그를 고정한 뒤, core 수정과 함께 해결해야 한다.

### 8.4 UNSUPPORTED 오용 금지

정책 문서(§10.3 / §11.3)에 **정의된 transport**가 실행 시 실패하면 반드시 `fail`로 보고해야 한다. `UNSUPPORTED`로 보고하여 실패를 숨기는 것을 **금지**한다.

| 상황 | 올바른 상태 | 설명 |
|------|------------|------|
| 정의된 transport가 정상 동작 | `success` | RESULT line 출력 |
| 정의된 transport가 실패 (timeout, crash, no_data 등) | `fail` | 원인 파악 후 수정 필요 |
| 정책에 정의되지 않은 transport 조합 | `unsupported` | 결과 제외 |
| stderr에 `protocol not supported` 포함 | `unsupported` | 런타임에서 지원되지 않는 transport 자동 감지 (multi 엔진만 지원, single 엔진은 stdout 토큰만 사용) |
| 플랫폼 제약으로 실행 불가 (예: Windows에서 ipc) | `skip` | reason 명시 필수 |

- `UNSUPPORTED`는 **정책에 정의되지 않은** pattern-transport 조합에만 사용한다.
- **stderr 기반 자동 분류**: 바이너리 stderr에 `protocol not supported` 문자열이 포함되면 multi 실행 엔진(`run_comparison.py`)이 해당 조합을 `unsupported`로 자동 분류한다. single 엔진(`single/run_comparison.py`)은 stderr 문자열 기반 분류를 수행하지 않으며 stdout `UNSUPPORTED` 토큰만 인식한다.
- 실패를 `UNSUPPORTED`로 위장하면 회귀(regression)가 감지되지 않으므로 엄격히 금지한다.

### 8.5 공통화 경계 원칙

perf 구현의 공통화 기준은 **코드 중복 제거 자체가 아니라 benchmark 의미 보존과
전체 복잡도 감소**다. 공통 helper는 지원 인프라를 숨길 수는 있지만, 패턴의
측정 의미, I/O 모델, routing/backpressure semantics를 숨겨서는 안 된다.

#### 공통화 권장 대상

아래 항목은 공통 헤더/공통 소스/공통 runner helper로 수렴하는 것을 권장한다.

| 항목 | 설명 |
|------|------|
| CLI 인자 파싱 | `argc`/`argv` 해석, 옵션 추출 |
| 환경 변수 해석 | `resolve_bench_msg_sizes`, `resolve_multi_bench_settings` 등 |
| 표 출력 포맷 | markdown table, `Effective Options`, failure summary |
| TLS 설정 | `setup_tls_client`, `setup_tls_server` |
| Context RAII | `ctx_guard_t` 등 리소스 관리 wrapper |
| 타이머/스톱워치 | `stopwatch_t`, 시간 측정 유틸리티 |
| Monitor 유틸리티 | raw=`CONNECTION_READY` counting helper, SPOT=`READY/START` barrier helper |
| transport 가용성 검사 | `transport_available()` |
| 공통 cleanup | socket / monitor / context close helper |

위 항목은 **벤치 의미를 바꾸지 않는 지원 인프라**로 분류한다.

#### 공통 구현 강제 대상

아래 항목은 구현체마다 중복 정의하지 말고, 공통 모듈의 단일 구현을 사용해야 한다.

| 항목 | 설명 |
|------|------|
| RESULT line 포맷팅/출력 | `RESULT,<lib>,<pattern>,...` 형식과 필드 순서를 단일 구현으로 유지 |
| metric header encode/decode | `magic`, `run_id`, `phase`, `msg_size`, `seq`, `sent_ts_us` 처리 |
| phase별 유효 샘플 판정 | active/warmup 구간 구분과 유효 header 판정 |
| throughput 계산 | 유효 수신 건수 기반 계산 |
| bandwidth 계산 | throughput와 payload size 기반 계산 |
| latency 샘플 집계 | header timestamp 기반 샘플 수집 |
| p95/p99 계산 | latency 분포 대표값 계산 |
| 메트릭 보정/출력 계약 | latency triplet 보정, metric naming, RESULT line 계약 유지 |
| CPU / 메모리 수집 | 리소스 메트릭 수집 및 보고 형식 |

이 항목들은 결과 해석과 조합 간 비교 가능성을 결정하는 **측정 계약**이므로
선택적 공통화가 아니라 **공통 구현 강제 대상**으로 본다.

#### 공통화 허용 기준

- 공통화의 목적은 코드 이동이 아니라 복잡도 감소여야 한다.
- single/multi가 같은 메트릭/출력 계약을 쓰도록 runner surface를 공통화한다.
- helper가 없어도 각 패턴의 측정 의미를 몇 문장으로 설명할 수 있어야 한다.
- helper는 설정, 출력, 정리, 계측 인프라를 감싸는 용도로만 사용한다.

#### 반드시 인라인 유지할 코어 로직

아래 로직은 각 benchmark 소스 파일 안에서 명시적으로 드러나야 한다.

- 해당 패턴이 사용하는 **send/recv API 호출** (어떤 zlink API를 쓰는지)
- routing frame 조립/해석
- `EAGAIN` 이후 pending flag / pending deque 처리 전략 (패턴별 backpressure
  방식이 다를 때)
- `recv + poller` 와 `callback + bounded queue + metric worker` 중 어떤 실행
  경로를 쓰는지
- ready gate 통과 이후 benchmark를 시작하는 실제 조건
- 소켓/handle 생성 및 연결 방식

즉, 파일 하나만 읽어도 해당 패턴이 **어떤 zlink API를 어떻게 사용하는지**
이해할 수 있어야 한다.

#### template policy 예외

동일 구조의 echo/relay 패턴에서 send/recv API 호출만 다르고 phase 제어,
poller event loop, latency 집계 등의 **공통 골격이 95% 이상 동일**한 경우,
아래 조건을 모두 만족하면 template header로 공통 골격을 추출할 수 있다.

- 각 패턴 파일이 **policy struct**로 send/recv API 호출을 명시적으로 정의한다.
  policy struct를 보면 해당 패턴이 어떤 zlink API를 쓰는지 즉시 알 수 있어야
  한다.
- 소켓/handle 생성, 연결, monitor-ready gate는 패턴 파일에 인라인으로 남긴다.
- template은 C++ template instantiation으로 컴파일 시 inline되어 **런타임
  비용이 0**이어야 한다. function pointer, virtual dispatch, `std::function`
  기반 간접 호출은 허용하지 않는다.
- template header 내부에 pattern별 분기(`if`/`switch`)가 없어야 한다.
  패턴 차이는 오직 template parameter(policy struct)로만 표현한다.
- 구조가 다른 패턴(one-way publish, stream framing, deque backpressure 등)을
  같은 template에 억지로 끼워 넣지 않는다.

이 예외는 **동일 코드 복붙을 줄여 유지보수 비용을 낮추기 위한 것**이며,
서로 다른 측정 구조를 하나의 추상화에 합치는 용도가 아니다.

#### 과도한 공통화 판정 기준

아래 중 하나라도 만족하면 과도한 공통화로 간주하고 분리한다.

- helper/template 안에 pattern별 분기(`if`/`switch`)가 늘어나 새 패턴 추가 시
  helper를 계속 수정해야 한다.
- backpressure, routing, phase 의미가 helper 내부로 숨어 파일만 봐서는 설명이
  안 된다.
- 공통화 이후 변경 증폭이 줄지 않고 오히려 여러 패턴이 한 helper에 결합된다.
- 구조가 다른 패턴을 하나의 template/helper에 합치기 위해 조건부 로직이
  추가된다.

#### STREAM client 예외 (검증 인프라)

`core/perf/common/streamclient/`의 STREAM raw/multi client 코드는
**벤치마크 대상 라이브러리 자체가 아니라 검증 인프라**로 간주한다.

- STREAM client 공통 구현은 `common/streamclient/`에 모아둘 수 있다.
- 이 예외는 STREAM 계열 client 인프라에만 적용한다.
- STREAM 계열은 multi suite에서만 테스트하므로 single suite에는 해당 없다.

---

## 8.6 리팩토링 원칙 (공통)

> 참조:
> [`doc/plan/refactor/00-core-system-posd-refactor-plan.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/refactor/00-core-system-posd-refactor-plan.ko.md),
> [`AGENTS.md`](/home/hep7/project/kairos/zlink/AGENTS.md)

perf 벤치마크 코드와 실행 인프라를 리팩토링할 때는 아래 원칙을 공통으로
적용한다. `core/perf/README*.md`는 사용 방법만 설명하며, 설계/리팩토링 기준은
본 정책 문서가 source of truth다.

### 8.6.1 성능 비회귀 우선

- 구조 변경은 single/multi 기준 성능을 저하시켜서는 안 된다.
- 각 리팩토링 단계는 full single + multi perf 실행으로 기준선 비회귀를 확인한 뒤
  다음 단계로 진행한다.
- 코드 품질이 개선되더라도 throughput/latency가 회귀하면 해당 변경을 수용하지
  않는다.

### 8.6.2 복잡도 감소가 목적이다

- 리팩토링은 코드를 옮기는 작업이 아니라 전체 복잡도를 줄이는 작업이어야 한다.
- 얕은 wrapper, pass-through 계층, config flag 기반 분기로 간접비만 늘리는
  구조를 제거한다.
- 각 계층은 단순 위임이 아니라 서로 다른 추상화를 제공해야 한다.

### 8.6.3 깊은 모듈과 명확한 ownership

- 넓은 호출 표면의 작은 함수 다발보다, 좁은 인터페이스와 풍부한 내부를 가진
  모듈을 선호한다.
- 소켓, 컨텍스트, 타이머, 파일 디스크립터 등 모든 리소스는 정확히 하나의
  authoritative close owner를 가져야 한다.
- lifecycle, ownership, invariant는 몇 문장으로 설명 가능해야 한다.

### 8.6.4 정보 은닉

- benchmark 바이너리는 라이브러리 내부 구조에 의존하지 않아야 한다.
- 패턴별 측정 의미와 프로세스 관리, 결과 포맷팅, 파일 I/O 같은 메커니즘을
  분리한다.
- phase machinery나 transport 내부를 패턴 수준 측정 코드에 노출하지 않는다.

### 8.6.5 retry / workaround / 인위적 흐름 제어 금지

- 스크립트와 바이너리에 retry 로직을 넣지 않는다.
- inflight/outstanding 제한 옵션으로 측정 의미를 왜곡하지 않는다.
- 실패를 `UNSUPPORTED`나 우회 로직으로 숨기지 않는다.
- 실패는 실제 신호로 취급하고 근본 원인을 수정한다.

### 8.6.6 죽은 코드와 레거시 옵션 정리

- 미사용 코드, retry 관련 변수, inflight 관련 변수, orphan helper는
  리팩토링 과정에서 제거한다.
- compatibility shim, `_unused` 류의 이름 변경, `// removed` 주석으로
  잔존물을 남기지 않는다.

### 8.6.7 구조로 오용을 방지한다

- 정책 문서나 런타임 검사에만 의존하지 말고 타입과 API 설계로 오용을 막는다.
- 예: RAII guard, enum-typed phase state, compile-time pattern/transport
  validation.

### 8.6.8 변경 증폭을 줄인다

- 새 pattern 추가는 새 소스 파일과 transport matrix entry 정도로 끝나야 한다.
- 새 transport 추가가 pattern-level 코드를 건드리게 만들면 경계가 잘못된 것이다.
- 한 곳의 변경이 여러 곳을 강제로 수정하게 만들면 추상화를 다시 설계해야 한다.

### 8.6.9 단계별 게이트

- 각 리팩토링 단계는 아래 게이트를 통과해야 한다.
  1. 기능 게이트: `run_test_lanes.sh`
  2. 성능 게이트: full single + multi perf 실행, 회귀 없음
  3. hot-path 게이트: 측정 경로에 새 lock/alloc/log 없음
- 현재 단계 게이트를 통과하기 전에는 다음 단계를 시작하지 않는다.

---

## 9. 환경 변수 (공통)

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_DEBUG` | 디버그 로그 | unset |
| `PERF_IO_THREADS` | context I/O threads | 0 |
| `PERF_MSG_SIZES` | 테스트 size 목록 (러너가 size별 케이스로 분할 실행). single 기본값은 `64,256,1024,65536,131072,262144`, multi 기본값은 `256,1024,65536,131072,262144` | suite/패턴별 기본값 |
| `PERF_TRANSPORTS` | 테스트 transport 목록 | suite/패턴별 기본값 |
| `PERF_TASKSET` | CPU pinning (`1`로 활성화, Linux: taskset, Windows: processor affinity) | 0 |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 | 0 |
| `PERF_MAX_SOCKETS` | context max sockets | auto |
| `PERF_DISABLE_RESOURCE_METRICS` | 리소스 메트릭(CPU/메모리) 수집 비활성화 (`1`로 활성화) | 0 |
| `PERF_RESULTS_MAX_FILES` | report/ 디렉터리 최대 파일 수 (multi 전용) | 100 |

- 위 환경 변수는 core와 모든 바인딩에서 동일하게 적용된다 (단, `PERF_RESULTS_MAX_FILES`는 multi 엔진만 참조하며, single 엔진은 100 하드코딩).
- suite별 고유 환경 변수는 개별 정책 문서를 참조한다:
  - Single: [PERF_SINGLE_TEST_POLICY.md § 11](PERF_SINGLE_TEST_POLICY.md)
  - Multi: [PERF_MULTI_TEST_POLICY.md § 12](PERF_MULTI_TEST_POLICY.md)

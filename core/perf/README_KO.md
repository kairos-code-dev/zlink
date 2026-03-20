# Core PERF 벤치마크 스크립트

zlink perf는 아래 두 개의 진입 스크립트로 실행한다.

- `run_benchmarks.sh`: single 패턴 실행기
- `run_benchmarks_multi.sh`: multi 패턴 래퍼

정책 source of truth는 `doc/perf/*.md`이며, 공식 결과 파일은 항상
`core/perf/results/.../report/` 아래에 저장된다.

---

## run_benchmarks.sh

single 패턴(PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER,
ROUTER_ROUTER, GATEWAY, SPOT)의 성능을 측정한다.

### 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--pattern NAME` | `ALL` | 패턴 목록(쉼표 구분) 또는 `ALL` |
| `--reuse-build` | 비활성 | 기존 빌드 디렉터리 재사용(configure/build 생략) |
| `--clean-build` | 비활성 | 빌드 디렉터리 삭제 후 클린 빌드 |
| `--build-dir PATH` | `core/build` | 공식 빌드 디렉터리만 허용 |
| `--output PATH` | — | 콘솔 출력을 파일로 tee |
| `--results-dir PATH` | `core/perf/results` | 결과 루트 경로 지정 |
| `--results-tag NAME` | — | 결과 파일명 태그 |
| `--runs N` | `1` | pattern/transport/size별 반복 횟수 |
| `--recv MODE` | `recv` | 수신 모델(`recv` 또는 `callback`) |
| `--duration N` | `5` | active 측정 시간(초) |
| `--warmup N` | `2` | single warmup 시간(초) |
| `--hwm N` | — | `PERF_SINGLE_HWM` fallback 설정 |
| `--send-hwm N` | — | `PERF_SINGLE_SNDHWM` 설정 |
| `--recv-hwm N` | — | `PERF_SINGLE_RCVHWM` 설정 |
| `--sndbuf SIZE` | — | `PERF_SINGLE_SNDBUF` 설정 (예: `64b`, `1k`, `64k`) |
| `--rcvbuf SIZE` | — | `PERF_SINGLE_RCVBUF` 설정 (예: `64b`, `1k`, `64k`) |
| `--sndtimeo N` | `200` | `PERF_SINGLE_SNDTIMEO_MS` 설정 (밀리초) |
| `--rcvtimeo N` | `200` | `PERF_SINGLE_RCVTIMEO_MS` 설정 (밀리초) |
| `--pin-cpu` | 비활성 | CPU 고정(Linux taskset) |
| `--io-threads N` | — | `PERF_IO_THREADS` 설정 |
| `--msg-sizes LIST` | — | 메시지 크기 목록(쉼표 구분) |
| `--transports LIST` | — | 트랜스포트 목록(쉼표 구분) |

참고: `pgm`/`epgm`은 single perf에서 현재 비활성화 상태다.

상세 phase 의미, handshake 규칙, mode 계약은
`doc/perf/PERF_SINGLE_TEST_POLICY.md`를 기준으로 본다.

현재 single recv 모드 지원 범위:

- `recv`: `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`, `GATEWAY`, `SPOT`
- `callback`: `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`, `GATEWAY`, `SPOT`

지원하지 않는 조합은 묵시적 fallback 없이 fail-fast로 종료한다.

### 결과 저장

```text
results/
  single/
    report/
      perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt
```

---

## run_benchmarks_multi.sh

multi 패턴 래퍼 스크립트다. multi 옵션을 정규화한 뒤 `PERF_ALLOW_MULTI=1`
환경으로 `run_benchmarks.sh`를 호출한다.

### 기본 패턴

`DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,GATEWAY,SPOT,STREAM`

### 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--pattern NAME` | 기본 전체 | 패턴 목록(쉼표 구분), `MULTI_` 접두어 생략 가능 |
| `--help` | — | 도움말 |
| `--reuse-build` | 비활성 | 기존 빌드 재사용 |
| `--clean-build` | 비활성 | 클린 빌드 |
| `--results-dir PATH` | `core/perf/results` | 결과 루트 경로 |
| `--results-tag NAME` | — | 파일명 태그 |
| `--build-dir PATH` | `core/build` | 공식 빌드 디렉터리만 허용 |
| `--output PATH` | — | 출력을 파일로 tee |
| `--runs N` | `1` | 설정별 반복 횟수 |
| `--recv MODE` | `recv` | 수신 모델(`recv` 또는 `callback`) |
| `--pin-cpu` | 비활성 | CPU 고정 |
| `--io-threads N` | — | 서버/클라이언트 io thread 동시 설정 |
| `--server-io-threads N` | non-stream=`2`, stream=`4` | 서버 io threads |
| `--client-io-threads N` | non-stream=`2`, stream=`4` | 클라이언트 io threads |
| `--msg-sizes LIST` | env/기본값 | 메시지 크기 목록 |
| `--transports LIST` | `tcp,tls,ws,wss` | 트랜스포트 목록 |
| `--warmup N` | `2` | warmup 시간(초) |
| `--duration N` | `5` | active 측정 시간(초) |
| `--clients N` | `100` (`stream=10000`) | 패턴별 클라이언트 수 |
| `--hwm N` | env/바이너리 기본값 | `PERF_MULTI_HWM` 설정 |
| `--send-hwm N` | `--hwm` fallback | `PERF_MULTI_SNDHWM` 설정 |
| `--recv-hwm N` | `--hwm` fallback | `PERF_MULTI_RCVHWM` 설정 |
| `--sndtimeo N` / `--send-timeout-ms N` | `200` | `PERF_MULTI_SNDTIMEO_MS` |
| `--rcvtimeo N` / `--recv-timeout-ms N` | `200` | `PERF_MULTI_RCVTIMEO_MS` |
| `--connect-concurrency N` | 자동 | 동시 연결 수 |
| `--transport-transition-ms N` | `3000` | 트랜스포트 전환 대기 |
| `--pattern-transition-ms N` | `3000` | 패턴 전환 대기 |
| `--server-ready-timeout-ms N` | `10000` | 서버 준비 대기 |
| `--connect-ready-timeout-ms N` | `5000` | 연결 준비 대기 |
| `--monitor-hwm N` | `1000` | 모니터 HWM |
| `--server-shutdown-timeout-ms N` | `5000` | 서버 종료 대기 |
| `--server-bind-port N` | `0` | 서버 바인드 포트 |

### 결과 저장

```text
results/
  multi/
    report/
      perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt
```

현재 multi recv 모드 지원 범위:

- `recv`: `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `STREAM`
- `callback`: `DEALER_DEALER`, `PUBSUB`, `GATEWAY`, `SPOT`, `STREAM`

지원하지 않는 조합은 묵시적 fallback 없이 fail-fast로 종료한다.

### 사전 검사

- nofile 가드 (`PERF_SKIP_NOFILE_CHECK=1`로 비활성화)
- 메모리 가드 (`PERF_SKIP_MEMORY_CHECK=1`로 비활성화)
  - `PERF_MULTI_MEMORY_BUDGET_PCT=70` — MemAvailable 대비 예산 비율(%)
  - `PERF_MULTI_MEMORY_BASE_MB=512` — 기본 메모리 예약(MB)
  - `PERF_MULTI_MEMORY_PER_CLIENT_KB=1024` — 클라이언트당 예상 메모리(KB)

---

## 공통 환경 변수

| 변수 | 의미 |
|------|------|
| `PERF_IO_THREADS` | I/O thread 수 |
| `PERF_MSG_SIZES` | 메시지 크기 override |
| `PERF_TRANSPORTS` | 트랜스포트 override |
| `PERF_RESULTS_DIR` | 결과 루트 경로 override |
| `PERF_RESULTS_TAG` | 파일명 태그 |
| `PERF_RESULTS_MAX_FILES` | report/ 디렉터리 최대 파일 수 (기본: 100) |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 (`1`) |
| `PERF_TASKSET` | CPU 고정 (`1`) |

single 상세 제약/정책은 `PERF_SINGLE_TEST_POLICY.md`,
multi 상세 제약/정책은 `PERF_MULTI_TEST_POLICY.md`를 따른다.

---

## 빠른 예시

single 전체 실행:

```bash
./core/perf/run_benchmarks.sh --build-dir /home/hep7/project/kairos/zlink/core/build
```

single 제한 실행:

```bash
./core/perf/run_benchmarks.sh \
  --pattern PAIR \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64,1024 \
  --runs 3 \
  --duration 5 \
  --recv recv
```

multi 전체 실행:

```bash
./core/perf/run_benchmarks_multi.sh --build-dir /home/hep7/project/kairos/zlink/core/build
```

multi STREAM callback 실행:

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --clients 5000 \
  --duration 10 \
  --recv callback \
  --transports tcp
```

---

## 리팩토링 원칙

> 참조: [Core System POSD 리팩토링 계획](../../doc/plan/refactor/00-core-system-posd-refactor-plan.ko.md),
> [AGENTS.md — Software Design Philosophy](../../AGENTS.md)

perf 벤치마크 코드 및 인프라 리팩토링 시 아래 원칙을 적용한다.
프로젝트 전체 POSD(A Philosophy of Software Design) 리팩토링 계획과
저장소 설계 철학에서 파생된다.

### 1. 성능 비회귀 (최우선)

- 구조 변경이 single/multi 벤치마크 기준선을 절대 저하시키지 않아야 한다.
- 모든 리팩토링 단계는 기록된 기준선 대비 전체 perf 실행
  (`run_benchmarks.sh`, `run_benchmarks_multi.sh`)을 통과해야 다음 단계로 진행한다.
- 코드 품질이 향상되더라도 throughput/latency가 회귀하면 해당 변경을 거부한다.

### 2. 복잡도 감소 — 코드 이동이 아닌 제거

- 리팩토링은 전체 시스템 복잡도를 줄여야 하며, 단순히 다른 곳으로 옮기지 않는다.
- 추상화를 추가하지 않는 얕은 래퍼, pass-through 계층, config 플래그 기반
  분기를 제거한다.
- 각 계층은 단순 위임이 아닌 **서로 다른 추상화**를 제공해야 한다.

### 3. 깊은 모듈, 명확한 소유권

- 많은 작은 함수와 넓은 호출 표면 대신, 좁은 인터페이스와 풍부한 내부를 가진
  모듈을 선호한다.
- 모든 리소스(소켓, 컨텍스트, 타이머, 파일 디스크립터)는 정확히
  **하나의 권위 있는 close 소유자**를 가져야 한다 — 관례가 아닌 구조(RAII,
  unique ownership)로 강제한다.
- 모든 컴포넌트의 생명주기, 소유권, 불변량은 몇 문장으로 설명 가능해야 한다.

### 4. 정보 은닉

- 벤치마크 바이너리는 라이브러리 내부 구조에 의존하지 않아야 한다.
- **의미적** 관심사(패턴별 측정 의미)와 **메커니즘** 관심사(프로세스 관리,
  결과 포맷팅, 파일 I/O)를 분리한다.
- phase 기계나 transport 내부를 패턴 수준 측정 코드에 노출하지 않는다.

### 5. 재시도 금지 / 우회 금지 / 인위적 흐름 제어 금지

- 스크립트 및 바이너리에 retry 로직 금지 ([PERF_POLICY.md § 8.1](PERF_POLICY.md)).
- inflight/outstanding 제한 옵션 금지 ([PERF_POLICY.md § 8.2](PERF_POLICY.md)).
- 실패를 `UNSUPPORTED`로 위장하는 것 금지 ([PERF_POLICY.md § 8.4](PERF_POLICY.md)).
- 실패는 실제 신호다 — 근본 원인을 수정하며, 절대 숨기지 않는다.

### 6. 죽은 코드 정리

- 미사용 코드, 레거시 환경 변수(`PERF_MULTI_ATTEMPTS`, retry 관련 변수,
  inflight 변수), 고아 헬퍼를 리팩토링의 일부로 제거한다.
- 호환성 shim, `_unused` 리네이밍, `// removed` 주석을 남기지 않는다.

### 7. 구조에 의한 오류 방지

- 런타임 검사나 정책 문서만으로가 아닌, 타입 시스템과 API 설계로 오용을 방지한다.
- 예시: RAII 컨텍스트 가드(`ctx_guard_t`), enum 타입 phase 상태,
  가능한 경우 컴파일 타임 패턴/transport 검증.

### 8. 변경 증폭 리트머스 테스트

- 리팩토링 후, 새 패턴 추가는 새 소스 파일과 transport 매트릭스 항목만
  필요해야 하며 — 공유 인프라 전반의 변경이 아니어야 한다.
- 새 transport 추가는 패턴 수준 코드를 건드리지 않아야 한다.
- 한 곳의 변경이 여러 곳의 변경을 강제하면 추상화 경계가 잘못된 것이다.

### 9. 단계별 게이트 진행

- 리팩토링은 단계별로 진행하며, 각 단계는 다음을 통과해야 한다:
  1. 기능 게이트 — `run_test_lanes.sh` (모든 테스트 레인 통과)
  2. 성능 게이트 — single + multi 전체 perf 실행, 회귀 없음
  3. 핫패스 게이트 — 측정 경로에 새로운 lock/동적할당/로깅 없음
- 현재 단계 게이트를 통과하기 전에 다음 단계를 시작하지 않는다.

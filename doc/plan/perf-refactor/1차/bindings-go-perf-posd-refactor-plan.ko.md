# `bindings/go/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **D+** — 전 언어 중 가장 심각한 중복과 산재
> 대상: `bindings/go/perf/`

## 1. 목표

Go perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- echo server의 recv/callback 분기가 공통 헬퍼로 통합되되, send/recv API 호출은 패턴 파일에서 보인다.
- 설정/TLS/결과 출력/metric header/percentile 계산이 공통 모듈로 통합된다.
- 측정 루프(send/recv API, EAGAIN 처리, ready gate, 소켓 생성)는 패턴 파일에 inline 유지된다.
- ready-wait 로직이 소켓 타입과 무관하게 하나의 구현으로 통합된다.
- `core/perf`와 동일한 측정 의미를 유지한다.

## 2. 현재 문제 요약

### 2.1 echo server 중복 (6회 반복)

`startPairEchoServer`, `startDealerEchoServer`, `startRouterEchoServer`,
`startRouterRouterEchoServer`, `startMultiRouterEchoServer`,
`startMultiStreamEchoServer` 등
6개의 echo server 함수가 20-30줄씩 80%+ 동일한 코드를 유지한다.

차이점은 소켓 타입과 send 시맨틱(`Send()` vs `SendTo()`)뿐이다.

### 2.2 recv/callback 분기 산재 (12+ 곳)

`if recvMode == "callback"` 분기가 single/ 6개, multi/ 6개 이상 파일에
반복된다. 에러 핸들링, 타임아웃, goroutine 생성이 매번 재구현된다.

### 2.3 single/multi 코드 트리 70-90% 동일

| single 파일 | multi 파일 | 유사도 |
|------------|-----------|--------|
| pair.go (87줄) | dealer_dealer.go (110줄) | 70% |
| pubsub.go (71줄) | pubsub.go (152줄) | 75% |
| dealer_router.go (130줄) | dealer_router.go (139줄) | 75% |
| router_router.go (112줄) | router_router.go (171줄) | 80% |
| spot.go (147줄) | spot.go (185줄) | 85% |

핵심 차이는 클라이언트 수와 subscriber 루프뿐이다.

### 2.4 ready-wait 5개 변종

`waitForSpotReady`, `waitForRouterReady` 등 5개 이상의 ready-wait 함수가
25-40줄씩, 60-80% 유사하게 존재한다.

### 2.5 common.go가 얕은 유틸리티만 제공

`perfcommon/common.go` (208줄)는 `Stats`, `PrintResult`, `CloneMessages` 등
기계적 유틸리티만 제공한다. echo server, ready-wait, 벤치마크 루프 같은
**테스트 프레임워크 추상화가 없다**.

### 2.6 transience 체크 50+ 회 반복

`perfcommon.IsTransient(err)` 호출이 50+ 곳에 분산되어 있고,
재시도/백오프 추상화가 없다.

## 3. 설계 원칙

- `core/perf`와 동일한 측정 의미 절대 우선
- Go 관용 스타일 유지 (interface, goroutine, channel)
- 공통화는 변경 증폭 축소가 목적이지 코드 줄 수 축소가 아님
- 패턴 고유 로직은 패턴 파일에 남김
- 환경 변수 해석을 한 곳으로 집중
- **inline 코드 요구사항** (`PERF_POLICY.md` 715-727줄):
  패턴 파일에서 send/recv API 호출, EAGAIN 처리, 모델 선택, ready gate,
  소켓 생성이 직접 보여야 한다. 설정/TLS/결과 출력만 공통화 가능.
- **매 단계 완료 시**:
  1. build + 테스트 통과
  2. smoke perf 실행: 정상 종료, RESULT line 정책 형식 출력, 결과 파일 생성 확인
     (수치 비교는 하지 않음 — 병렬 작업으로 측정값 왜곡 가능)
  3. hot-path에 새 lock/alloc/log 없음 확인
  4. full comparable run + 수치 비교는 **전체 리팩토링 완료 후 순차 실행**

## 4. 단계별 실행 계획

### 단계 0. 현황 동결

할 일:
- 파일별 줄 수, 중복도 표 작성
- recv/callback 분기 위치 전수 목록
- echo server 함수 시그니처와 차이점 표
- ready-wait 함수 시그니처와 차이점 표
- env var 전수 목록
- `doc/perf/PERF_POLICY.md` recv/callback 모드 매트릭스 감사:
  - single: callback-only (전 패턴, 예외 없음)
  - multi: recv-only (SPOT/STREAM만 dual-mode)
  - 미지원 조합이 fail-fast하는지 확인 (silent fallback 금지)
- STREAM 공유 클라이언트 경로 처리 현황 확인
- direct native API 사용 현황 (`zlink_` C API grep)
- `core/perf` 대비 semantic parity 체크리스트:
  - throughput 정의 (msgs/sec)
  - bandwidth 정의 (throughput × size / 1,000,000)
  - latency sample set (active phase 유효 메시지)
  - phase 구조 (ready → warmup → active)
  - RESULT line 형식
  - recv_mode / direction 일치

완료 기준:
- 어떤 파일에서 무엇을 통합/제거/이동할지 파일 단위로 정리됨
- PERF_POLICY 위반 목록이 작성됨

### 단계 1. phase drain 삭제 (recv drain loop는 유지)

**중요 구분**: `drainSpotOnce()` 등이 poller event loop 안의 nonblocking recv
(recv drain loop)인지, warmup→active 전환용 별도 phase인지 구분 필요.

할 일:
- spot의 500ms drain 루프 삭제 (`single/spot.go:77-80`, `multi/spot.go:95-98`)
  — 이것은 시간 기반 별도 phase이므로 삭제 대상
- `drainMultiPubSubAvailable()` (`multi/pubsub.go:121`): pubsub subscription
  확인용이면 ready gate로 흡수, 단순 phase drain이면 삭제
- `drainSpotOnce()`, `drainMultiSpotOnce()`: poller event loop 내 nonblocking
  recv이면 이름만 변경하고 유지, 별도 phase이면 삭제
- 각 함수의 용도를 코드에서 확인한 후 삭제/유지 판단
- warmup 메시지가 active latency에 혼입되지 않는지 확인
  (header phase 필드로 걸러지는지 확인)

완료 기준:
- 별도 phase drain (시간 기반 drain 루프, settle 등) 0건
- recv 모델의 nonblocking recv loop는 정상 유지
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 2. echo server 중복 축소 (recv/callback 분기는 패턴 파일에 유지)

할 일:
- `perfcommon/`에 공통 타입/상수만 추가 (`RecvMode` 타입 등)
- echo server의 **goroutine 생성, recv loop, callback 등록, send API 호출,
  recv/callback 분기는 각 패턴 파일에 inline 유지** (`PERF_POLICY.md` 715-727줄)
- 중복 축소는 TLS 설정, 소켓 옵션, 결과 출력 등 측정 인프라에 한정
- 패턴 파일을 읽으면 어떤 실행 경로(recv+poller vs callback)를
  사용하는지 직접 보여야 함

완료 기준:
- TLS/옵션/결과 출력 중복 제거
- recv/callback 분기와 send/recv API 호출이 패턴 파일에서 직접 보임

### 단계 3. ready-wait 통합

할 일:
- `perfcommon/` 에 통합 ready-wait 추가:

```go
type ReadyConfig struct {
    Monitor     *zlink.SocketMonitor
    MinEvents   int
    Timeout     time.Duration
}

func WaitReady(cfg ReadyConfig) error
```

- 5개 ready-wait 변종을 이 헬퍼로 교체
- SPOT의 explicit barrier는 별도 함수로 유지하되 공통 타임아웃/에러 처리 공유

완료 기준:
- ready-wait 함수 5개 → 1-2개
- 타임아웃 변경 시 수정 포인트 1곳

### 단계 4. 측정 인프라 공통화 (측정 루프는 inline 유지)

할 일:
- `perfcommon/` 에 측정 인프라만 추가:
  - `StampPayload()` / `SentAtFromMessage()` — metric header encode/decode
  - `ComputeLatencyStats()` — percentile 계산
  - `PrintResult()` — RESULT line 출력
  - `IsTransient()` — transience 판정 (공통 함수로 1곳에 유지)
- **측정 루프(send/recv API 호출, EAGAIN 처리, phase 경계, 소켓 생성)는
  각 패턴 파일에 inline으로 유지** (`PERF_POLICY.md` 715-727줄 요구사항)
- 패턴 파일에서 `IsTransient()` 호출은 inline으로 남기되 구현은 공통 모듈

완료 기준:
- metric header/percentile/RESULT 출력이 공통 모듈 1곳
- send/recv API, EAGAIN 처리, ready gate는 패턴 파일에서 직접 보임
- transience 구현 1곳

### 단계 5. single/multi 공통 유틸리티 통합

할 일:
- single/multi는 **별도 바이너리/디렉터리 유지** (정책상 single=callback only,
  multi=recv default, 결과 디렉터리 분리)
- 공통 유틸리티만 `perfcommon/`으로 추출:
  - TLS 설정
  - 소켓 옵션 적용
  - 결과 출력/포맷
  - env var 해석
- 각 패턴 파일의 토폴로지(소켓 생성, bind/connect)와
  측정 루프(단계 3에서 추출한 `RunBench` 호출)는 패턴 파일에 유지

완료 기준:
- single/multi 디렉터리 구조 유지
- 공통 유틸리티 중복 제거
- 패턴 파일에서 설정/TLS/출력 보일러플레이트 제거됨

### 단계 6. 환경 변수 집중화

할 일:
- `perfcommon/config.go` 에 모든 env var 해석 집중:

```go
type PerfConfig struct {
    Duration    time.Duration
    Warmup      time.Duration
    MsgSizes    []int
    Transports  []string
    RecvMode    RecvMode
    Clients     int
    // ...
}

func LoadConfig() PerfConfig
```

- 패턴 파일에서 직접 env var 읽기 제거

완료 기준:
- env var 읽기 포인트가 config.go 1곳으로 집중됨

### 단계 7. 검증

할 일:
- build 확인
- single/multi 전 패턴 smoke 실행
- `core/perf` 대비 semantic parity 확인
- recv/callback 양쪽 모드 정상 동작 확인
- 결과 파일이 `bindings/go/perf/results/` 아래 저장되는지 확인

완료 기준:
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지
- direct native API grep 0건 (Go binding API만 사용)
- `doc/perf/PERF_POLICY.md` recv/callback 모드 제약이 코드에서 강제됨
- 미지원 recv 모드 조합이 fail-fast

## 5. 패치 순서 권장안

1. phase drain 삭제
2. echo server 측정 인프라 공통화 (TLS/옵션/결과 출력)
3. ready-wait 공통화
4. 측정 인프라 공통화 (metric header/percentile/transience)
5. single/multi 공통 유틸리티 통합
6. env var 집중화
7. dead code 삭제 + 검증

## 6. 완료 정의

- 측정 인프라(TLS/metric header/percentile/RESULT 출력)가 공통 모듈로 통합됨
- recv/callback 분기, send/recv API 호출, EAGAIN 처리, ready gate는
  각 패턴 파일에서 직접 보임 (inline 유지)
- single/multi 디렉터리 구조 유지
- phase drain/settle 0건
- `core/perf`와 동일한 측정 의미 유지
- 전체 패턴/전체 사이즈 정상 동작

---

## 완료 상태

> 완료일: 2026-04-06
> 검증: 빌드 통과 + 스모크 테스트 통과 + 완료 정의 대비 코드 리뷰 통과

단계 0-7 완료. 측정 인프라 공통화, ready-wait 통합, 중복 심볼 삭제, dead code 삭제, settle 삭제가 반영되었고, 현재 완료 상태와 충돌하는 문구는 없다. `go build ./...` 통과, 스모크 RESULT 5줄.

# `bindings/java/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **B+** — PerfUtil 추상화 양호, 패턴 보일러플레이트와 recv 모드 비일관성
> 대상: `bindings/java/perf/`

## 1. 목표

Java perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- 패턴 파일 간 90%+ 동일한 보일러플레이트가 공통 프레임워크로 추출된다.
- single/multi 간 recv 모드 정책이 코드에서 명시적으로 표현된다.
- multi server recv 루프 중복이 통합된다.
- `core/perf`와 동일한 측정 의미를 유지한다.

## 2. 현재 문제 요약

### 2.1 single 패턴 파일 90%+ 보일러플레이트

- `PerfPair.java` (70줄) vs `PerfDealerDealer.java` (69줄): 소켓 타입만 다름
- `PerfDealerRouter.java` vs `PerfRouterRouter.java`: 유사 구조
- 공통 패턴: 소켓 생성 → TLS 설정 → monitor ready → `SingleSendLoops` 호출

`SingleSendLoops.java`가 측정 루프를 추상화한 것은 좋으나,
소켓 생성/설정/모니터 부분이 여전히 반복.

### 2.2 multi server recv 루프 중복

- `PerfMultiDealerDealer.runServer()` (27줄)
- `PerfMultiRouterRouter.runServer()`: 유사 구조
- recv loop + echo send가 패턴마다 반복

### 2.3 single=callback / multi=recv 비일관성

- Config에 `--recv` 플래그 존재 (`PerfUtil.java:301`)
- single 패턴은 항상 `onReceive` callback 사용
- multi 패턴은 항상 `recv()` 사용
- Config 옵션이 존재하지만 실제로 무시됨

### 2.4 TLS 설정 반복

각 패턴 파일에서 `ConfigureTls*(socket)` 호출이 반복.
소켓 생성 + TLS 설정 + 옵션 적용을 하나의 팩토리로 묶을 수 있음.

## 3. 설계 원칙

- `core/perf`와 동일한 측정 의미 절대 우선
- Java 관용 스타일 유지 (interface, builder 패턴)
- PerfUtil.java의 기존 추상화 유지 및 확장
- 불필요한 Config 옵션은 제거하거나 구현
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
- single 패턴 간 diff 매핑
- multi server 간 diff 매핑
- recv 모드 설정과 실제 사용 불일치 목록
- `doc/perf/PERF_POLICY.md` recv/callback 모드 매트릭스 감사:
  - single: callback-only (전 패턴, 예외 없음)
  - multi: recv-only (SPOT/STREAM만 dual-mode)
  - 미지원 조합이 fail-fast하는지 확인
- STREAM 공유 클라이언트 경로 현황 확인
- direct native API 사용 현황 (JNI 직접 호출 grep)
- `core/perf` 대비 semantic parity 체크리스트:
  - throughput/bandwidth/latency 정의 일치
  - phase 구조 (ready → warmup → active)
  - RESULT line 형식
  - recv_mode / direction 일치

완료 기준:
- 통합 대상이 함수 단위로 정리됨
- PERF_POLICY 위반 목록이 작성됨

### 단계 1. phase drain 삭제 (recv drain loop는 유지)

**중요 구분**: recv drain loop (poller POLLIN→nonblocking recv)는 유지.
`PHASE_DRAIN` enum과 그것을 사용하는 별도 phase 코드만 삭제.

할 일:
- `PHASE_DRAIN = 3` 상수 삭제 (`PerfUtil.java:32`)
- `PHASE_DRAIN` 사용하는 모든 별도 phase 코드 경로 삭제
- recv 모델의 poller event loop 내 nonblocking recv은 유지
- warmup 메시지가 active latency에 혼입되지 않는지 확인

완료 기준:
- `PHASE_DRAIN` enum grep 0건
- recv 모델의 nonblocking recv loop 정상 유지
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 2. TLS/옵션 헬퍼 추출

할 일:
- `PerfUtil`에 TLS/옵션 적용 helper만 추가
- **소켓 생성(`ctx.createSocket()`)과 bind/connect는 각 패턴 파일에
  inline으로 유지** (`PERF_POLICY.md` 715-727줄)
- 각 패턴 파일에서 TLS/옵션 적용 부분만 헬퍼 호출로 교체

완료 기준:
- TLS/옵션 적용이 공통 헬퍼 1곳
- 소켓 생성/bind/connect는 패턴 파일에서 직접 보임

### 단계 3. single 측정 인프라 공통화 (측정 루프는 inline 유지)

할 일:
- `SingleBenchRunner` 같은 통합 러너는 만들지 않음
- 공통화 범위: metric header decode, latency 집계, RESULT 출력, TLS 설정
- **소켓 생성, monitor ready, send API 호출, callback 등록은
  각 패턴 파일에 inline 유지** (`PERF_POLICY.md` 715-727줄)
- `SingleSendLoops.java`의 기존 추상화는 유지 (이미 send 클로저 기반)

완료 기준:
- metric/RESULT 출력이 공통 모듈
- 소켓 생성/ready/send API는 패턴 파일에서 직접 보임

### 단계 4. multi server 측정 인프라 공통화

할 일:
- `MultiServerRunner`는 만들지 않음
- 공통화 범위: RESULT 출력, metric helper, TLS/설정 해석
- **recv loop와 echo send API 호출은 각 패턴 파일에 inline 유지**

완료 기준:
- RESULT/metric 인프라가 공통 모듈
- recv loop/echo send는 패턴 파일에서 직접 보임

### 단계 5. recv 모드 정책 명시화

할 일:
- `doc/perf/PERF_POLICY.md` 기준으로 recv 모드 정책 정렬:
  - single: callback-only (전 패턴, 예외 없음)
  - multi: recv 기본 (SPOT/STREAM만 `--recv recv|callback` dual-mode)
- 미지원 조합 요청 시 fail-fast 구현 (silent ignore 제거)
- PerfUtil.java의 Config default가 PERF_POLICY와 일치하는지 확인
  (현재 single default=callback, multi default=recv → 정책과 일치)

완료 기준:
- Config 옵션과 실제 동작이 일치

### 단계 6. 검증

할 일:
- `gradle build` 확인
- single/multi 전 패턴 smoke
- `core/perf` 대비 semantic parity 확인
- 결과 파일 `bindings/java/perf/results/` 확인

완료 기준:
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지

## 5. 완료 정의

- TLS/옵션 설정 헬퍼가 공통 모듈로 통합됨
- 소켓 생성/bind/connect/send/recv API는 각 패턴 파일에서 직접 보임
- metric/RESULT 인프라가 공통 모듈로 통합됨
- recv 모드 Config와 실제 동작이 PERF_POLICY와 일치함
- 미지원 recv 모드 조합이 fail-fast
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지
- direct JNI 호출 0건 (Java binding API만 사용)

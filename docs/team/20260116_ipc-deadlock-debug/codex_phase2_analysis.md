# Codex Phase 2 분석 결과

## 결론 요약
- **가장 유력한 원인: Hypothesis 1 (ASIO 단일 thread 가정 오류) + on_read_complete()/restart_input() 레이스**.
- **권장 옵션: Option A (Strand 직렬화)**. 먼저 가장 낮은 변경 비용으로 레이스 제거.
- Strand 적용 후에도 10K가 0%라면 **Option B (Speculative Read)**를 2차로 검토.
- `i_asio_transport::read_some()` 추가는 가능하지만 **API/플랫폼/다중 transport 비용이 큼**.

## 핵심 질문 답변

### 1) Hypothesis 1-4 중 가장 유력한가?
**Hypothesis 1이 가장 유력**합니다.
- Phase 2a/2b에서 **70%까지 개선**되었으나 완전 해결이 안 됨 → _input_stopped와 _read_pending 상태 전이가 레이스에 의해 무너지는 패턴이 유력.
- Phase 2c에서 조건 제거 시 50%로 악화 → **중복 read 등록 또는 재진입**이 문제임을 시사.
- 즉, 논리 자체보다 **동시성/순서 보장 실패**가 더 큰 원인.

### 2) Strand 직렬화를 구현하면 해결되는가?
**가장 가능성이 높은 1차 해결책**입니다.
- `on_read_complete()`, `restart_input()`, `flush()` 경로가 같은 strand에서 실행되면 상태 전이가 선형화됨.
- 재귀로 인해 `_input_stopped` assert가 깨지는 문제도 **strand로 재진입을 순차화**하면 완화됨.
- 단, IPC 초고속 환경에서 **event loop tick 지연 자체**가 병목이면 100% 해결은 보장 불가.

### 3) 근본적으로 Speculative Read (동기 read)가 필수인가?
**필수라고 단정할 근거는 아직 부족**합니다.
- Phase 2a/2b에서 일정 수준 개선이 있었음 → 순서/레이스 문제가 맞다면 strand만으로도 해결 가능.
- Speculative Read는 **재설계 비용이 높고** transport별 구현·테스트 부담이 큼.
- 따라서 **Strand 적용 후 실패 지속 시**에만 “필수” 여부를 재평가하는 것이 현실적.

### 4) i_asio_transport에 read_some() 추가가 현실적인가?
**현실적으로 가능하지만 비용이 큼**.
- 구현 대상: tcp, ipc, ws, wss, ssl (최소 5개)
- 동기 read 구현은 플랫폼별 소켓 모드/락/에러 경로를 모두 커버해야 함.
- 인터페이스 확장으로 인해 **상위 로직 변경 + 테스트 확대**가 동반됨.
- 따라서 **즉시 적용보다는 2차 옵션**으로 판단.

## 권장 옵션

**Option A (Strand 직렬화) 권장**
- 이유: 변경 비용 최소, 레이스 제거에 가장 직접적, Phase 2 결과와 일치하는 개선 방향.
- 목표: `restart_input()`과 `on_read_complete()`의 **단일 실행 순서 보장**.

### Option A 구현 구체안

1) **strand 멤버 추가**
- `asio_engine`에 `asio::strand`(또는 `asio::io_context::strand`) 추가

2) **핸들러 전부 strand에 묶기**
- `start_async_read()`에서 `bind_executor(strand, handler)` 사용
- `on_read_complete()` 진입은 반드시 strand 내에서 실행

3) **restart_input()을 strand로 직렬화**
- 외부 호출 시 즉시 로직을 돌리지 말고
  - `asio::dispatch(strand, ...)` 또는 `asio::post(strand, ...)` 사용
- 재귀 호출 대신 **loop 구조로 큐잉**하여 스택/재진입 방지

4) **상태 전이 보호**
- `_input_stopped`, `_read_pending`, `_pending_buffers` 변경은 strand 내부에서만 수행
- debug 로그에 thread id/strand 여부 출력 (필요 시)

5) **flush()의 재진입 방지**
- flush 직후 재귀 호출 패턴 제거
- flush 이후 상태만 점검하고 **필요 시 post로 다음 tick 재시도**

### 기대 효과
- 상태 전이 선형화 → missed wakeup 감소
- 중복 read 등록 방지
- 재귀로 인한 assertion failure 제거

## Option B (Speculative Read) 적용 시 구현 요약
- `i_asio_transport::read_some()` 추가
- `restart_input()`에서 동기 read 시도 후
  - 읽은 바이트가 있으면 즉시 처리
  - 없으면 기존 async_read
- **모든 transport에 구현 필요**
- Windows/Unix 공통 경로 정합성 확보 필요

## Option C/D 평가
- **Option C**(재귀 depth 제한, yield 등)는 symptom 완화 가능성은 있으나
  - 레이스 자체 해결이 아님 → 70% 이상 개선 기대 어려움
- **Option D**는 현재 실패율(30%)과 10K 0%를 고려하면 실용성 낮음

## 제안하는 실행 순서
1) Option A 적용 및 2K/10K 재테스트
2) 개선 미흡 시 Option B 설계/스파이크 (특히 IPC 우선)
3) 필요 시 Option C의 보조적 안정화 도입

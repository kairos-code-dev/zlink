# `bindings/python/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **B-** — 유틸리티 추상화 양호하나 common 파일 중복과 multi client 반복
> 대상: `bindings/python/perf/`

## 1. 목표

Python perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- `perf_common.py`와 `perf_multi_common.py`의 중복 유틸리티가 하나로 통합된다.
- multi client의 반복 threading 패턴이 하나의 클라이언트 러너로 추상화된다.
- single 패턴 파일의 poller 설정 보일러플레이트가 공통 헬퍼로 추출된다.
- `core/perf`와 동일한 측정 의미를 유지한다.

## 2. 현재 문제 요약

### 2.1 perf_common.py / perf_multi_common.py 유틸리티 동일 복사

다음 함수가 두 파일에 동일하게 존재:

| 함수 | perf_common.py | perf_multi_common.py |
|------|---------------|---------------------|
| `latency_us_from_message` | 114-116줄 | 91-93줄 |
| `result_metrics` | 127-137줄 | 104-114줄 |
| `percentile` | 119-124줄 | 96-101줄 |
| `print_result_lines` | 140-143줄 | 117-119줄 |
| `wait_socket_event` | 146-163줄 | 122-143줄 |

정책 변경 (예: latency 계산식 수정) 시 두 파일 모두 수정 필요.

### 2.2 multi client threading 패턴 5회 반복

- `perf_multi_dealer_dealer_client.py` (77줄)
- `perf_multi_dealer_router_client.py` (75줄)
- `perf_multi_router_router_client.py` (76줄)
- `perf_multi_pubsub_client.py` (79줄)
- `perf_multi_spot_client.py` (유사 구조)

모든 파일이 동일한 패턴:
1. `worker(sock)` 함수 정의 (recv loop + lock 기반 metrics 수집)
2. `threading.Thread(target=worker)` 생성
3. 메인 스레드에서 send loop
4. 결과 수집

차이는 소켓 타입과 send/recv 시맨틱뿐.

### 2.3 single 패턴 poller 보일러플레이트

- `perf_pair.py` (75줄): Poller 생성 → 소켓 등록 → safe_poll 루프
- `perf_pubsub.py` (79줄): 거의 동일한 구조

Poller 설정 + safe_poll 루프가 각 패턴 파일에서 반복.

### 2.4 single=polling / multi=callback 비일관성

- single: `Poller` + `safe_poll()` 기반
- multi: `on_receive` callback + threading 기반
- 같은 metrics 수집 로직이 두 가지 다른 방식으로 구현됨

## 3. 설계 원칙

- `core/perf`와 동일한 측정 의미 절대 우선
- Python 관용 스타일 유지 (duck typing, context manager)
- 공통화는 import 가능한 단일 모듈로 추출
- threading 패턴은 클로저/팩토리로 캡슐화
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
- 두 common 파일 간 함수 대응표
- multi client 파일 간 diff 매핑
- `doc/perf/PERF_POLICY.md` recv/callback 모드 매트릭스 감사:
  - single: callback-only (전 패턴, 예외 없음)
  - multi: recv-only (SPOT/STREAM만 dual-mode)
  - 미지원 조합이 fail-fast하는지 확인
- STREAM 공유 클라이언트 경로 현황 확인
- direct native API 사용 현황 (ctypes/cffi 직접 호출 grep)
- `core/perf` 대비 semantic parity 체크리스트:
  - throughput/bandwidth/latency 정의 일치
  - phase 구조 (ready → warmup → active)
  - RESULT line 형식
  - recv_mode / direction 일치

완료 기준:
- 통합/제거 대상이 함수 단위로 정리됨
- PERF_POLICY 위반 목록이 작성됨

### 단계 1. phase drain 삭제 (recv drain loop는 유지)

**중요 구분**: recv drain loop와 phase drain을 구분해야 한다.

할 일:
- `_drain_ready()` (`perf_multi_pubsub_client.py:16`): pubsub subscription
  확인용이면 ready gate로 흡수, warmup→active 전환용 별도 phase이면 삭제
- `drain_len32be_frames()` (`perf_multi_common.py:201`): STREAM 프레임 파싱이므로
  **삭제하지 않고 이름을 `parse_len32be_frames()`로 변경**
- recv 모델의 poller safe_poll→recv loop는 유지
- warmup 메시지가 active latency에 혼입되지 않는지 확인

완료 기준:
- 별도 phase drain 함수 0건
- recv drain loop 정상 유지
- `drain_len32be_frames`가 `parse_len32be_frames`로 이름 변경됨
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 2. 공통 유틸리티 통합

할 일:
- `perf_metrics.py` 신규 모듈 생성 (또는 기존 `perf_common.py` 확장):
  - `latency_us_from_message()`
  - `percentile()`
  - `result_metrics()`
  - `print_result_lines()`
  - `wait_socket_event()`
  - `CallbackMetrics` 클래스
- `perf_common.py`와 `perf_multi_common.py`가 이 모듈을 import
- 각 파일에서 중복 함수 정의 삭제

완료 기준:
- 위 함수 정의가 1곳에만 존재
- 기존 import 경로 호환 유지

### 단계 3. multi client 측정 인프라 공통화

할 일:
- `perf_multi_common.py`에 측정 인프라만 추가:
  - metric header encode/decode, latency 집계, RESULT 출력
- `run_multi_client` 같은 통합 러너는 만들지 않음
- **소켓 생성, worker 스레드 생성, send/recv loop, lock 기반 metrics는
  각 패턴 파일에 inline 유지** (`PERF_POLICY.md` 715-727줄)

완료 기준:
- metric/RESULT 인프라가 공통 모듈
- send/recv/threading은 패턴 파일에서 직접 보임

### 단계 4. single callback-only 정렬

할 일:
- `PERF_SINGLE_TEST_POLICY.md:67-76`: single은 callback-only, poller 미사용
- 현재 single이 poller 기반이면 callback + bounded queue + metric worker
  모델로 전환
- `run_poller_bench()` 같은 poller 프레임워크는 만들지 않음
- callback 등록, send API 호출은 각 패턴 파일에 inline 유지

완료 기준:
- single 패턴이 callback-only 모델로 동작
- single 측정 경로에 poller 사용 0건

### 단계 5. 검증

할 일:
- single/multi 전 패턴 smoke
- recv/callback 양쪽 모드 정상 동작
- `core/perf` 대비 semantic parity 확인
- 결과 파일 `bindings/python/perf/results/` 확인

완료 기준:
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지

## 5. 완료 정의

- `perf_common.py` / `perf_multi_common.py` 유틸리티 중복 0건
- multi client의 metric 수집/결과 출력이 공통 모듈로 통합됨
- single은 callback-only 모델로 정렬됨 (poller 기반 single 경로 제거)
- send/recv API, threading, 모델 선택은 각 패턴 파일에서 직접 보임
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지
- `doc/perf/PERF_POLICY.md` recv/callback 모드 제약이 코드에서 강제됨
- 미지원 recv 모드 조합이 fail-fast
- direct native API 호출 0건 (Python binding API만 사용)

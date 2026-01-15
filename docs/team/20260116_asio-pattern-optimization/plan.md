# ASIO 사용 패턴 최적화 계획

## 1. 문제 분석
- 현 구현(src/asio/asio_poller.cpp:177-217)에서 `start_wait_read()`가 이벤트 처리마다 새로운 lambda와 `async_wait`를 재등록함.
- 재등록 비용이 누적되어 이벤트당 약 295ns 오버헤드 발생.
  - Lambda 생성: ~75ns
  - async_wait 등록: ~150ns
  - Callback dispatch: ~50ns
  - 재귀 호출: ~20ns
- libzmq-ref의 epoll(level-triggered) 구현은 최초 1회 등록 후 지속적으로 이벤트를 받는 구조로 오버헤드가 ~10ns 수준.
- 결과적으로 처리 경로에서 -28% ~ -36% 성능 격차가 발생하며 목표치는 -5% 이내.

## 2. 해결 방안
- ASIO handler 재사용 패턴 도입(once-shot 특성 고려):
  - `async_wait`는 one-shot이므로 "재등록 제거"가 아니라 "Handler 재사용을 통한 오버헤드 최소화"로 정리.
  - `poll_entry_t`에 멤버 핸들러(예: 멤버 함수 바인딩 또는 전용 Handler 객체)를 두고, 해당 핸들러가 필요 시 재등록을 수행.
  - Handler가 캡처하는 상태를 최소화해 lambda 생성 비용을 제거하고, 동일 객체를 반복 사용.
- 핵심은 “이벤트마다 새 lambda 생성” → “고정 핸들러 재사용 + 필요한 경우만 재등록”으로 전환.

## 3. 구현 단계
1. **현재 동작 분석 및 기준 성능 확보**
   - `asio_poller.cpp`의 `start_wait_read()`/`start_wait_write()` 호출 경로와 재등록 빈도 확인.
   - 기존 벤치마크 결과 기록(현재 오버헤드/throughput/latency).
2. **핸들러 구조 변경 설계**
   - persistent handler로 유지될 수 있도록 `asio_poller.hpp`에 멤버 핸들러/상태 필드 추가.
   - “등록 1회” 패턴을 적용할 lifecycle 정의(등록 시점, 종료 조건, 재등록 필요 조건).
3. **코드 수정 적용**
   - `start_wait_read()`/`start_wait_write()`에서 lambda 생성 제거.
   - `poll_entry_t`의 멤버 핸들러(또는 전용 Handler 객체)로 `async_wait`를 호출.
   - 핸들러 내부에서 이벤트 처리 후 필요 시 `async_wait` 재등록(관심 상태가 유지되는 경우).
   - `BOOST_ASIO_CUSTOM_HANDLER_ALLOCATION` 등 ASIO 내부 최적화 훅 적용 검토.
4. **이벤트 관심 상태 변경 관리**
   - `set_pollin/reset_pollin` 및 `set_pollout/reset_pollout` 시 기존 `async_wait` 처리 방식을 명시.
   - 기본안: 관심 상태 변경 시 `cancel()`로 기존 대기 종료 → 상태 갱신 → 필요 시 재등록.
   - 관심 상태가 모두 off가 되면 재등록하지 않고 정리 경로로 이동.
5. **참조 구현 검증**
   - libzmq-ref/src/epoll.cpp의 level-triggered 패턴과 비교하여 관심 이벤트 관리 방식 정렬.
6. **측정 및 튜닝**
   - 변경 후 오버헤드 측정(1차 목표: ~50-80ns 수준).
   - 필요 시 추가 최적화(불필요한 분기/락 제거, 상태 전환 최소화).

## 4. 예상 성능 개선
- 이벤트당 오버헤드: ~295ns → ~50-80ns(1차 목표).
- 전체 성능 격차: -28% ~ -36% → 목표 -5% 이내 도달 가능.

## 5. 테스트 계획
- 빌드 및 테스트:
  - `./build.sh` (clean build + tests)
  - 또는 `./build-scripts/linux/build.sh x64 ON`
- 벤치마크:
  - 기존 성능 측정에 사용한 벤치/툴을 동일 조건으로 실행하여 비교.
  - 추가적으로 `perf/` 또는 `benchwithzmq/`에서 관련 벤치 수행.

## 6. 리스크 분석
- **이벤트 관심 상태 불일치**: persistent 핸들러 유지 시 read/write 관심 상태가 올바르게 갱신되지 않으면 이벤트 누락 가능.
- **operation_aborted 처리 누락**: `cancel()`로 인해 `boost::asio::error::operation_aborted`가 발생할 수 있으므로 복구/재등록 로직 필요.
- **에러 핸들링 누락**: 기존 재등록 기반에서 자연스럽게 갱신되던 에러 처리 경로가 빠질 위험.
- **리소스 해제 시점 문제**: 핸들러가 지속적으로 살아있으므로 종료 시점에서 취소/정리 누락 가능.
- **Race condition**: `stop()` 시점에 async_wait 핸들러가 dangling pointer를 참조하지 않도록 방어 설계 필요.
- **ASIO 특성 차이**: backend에 따라 level-triggered 동작이 완전히 동일하지 않을 수 있음.

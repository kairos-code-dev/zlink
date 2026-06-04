# Spot Shutdown / Teardown 이슈에 대한 의견

기준 문서: `doc/plan/direct-callback-recv/claude-spot-shutdown-review-request.ko.md`

## 한 줄 결론
현재 증상의 1순위 원인은 `spot` 자체의 개별 teardown 순서가 아니라
`service_runtime_base_t`가 timeout 시 아직 제거되지 않은 socket을
추적 집합에서 잃어버리는 구조라고 본다.

즉 지금 보이는 `shutdown=abortive` 로그는 "강제 종료를 시도했다"는 뜻이지,
남은 socket을 끝까지 추적해서 정리했다는 뜻이 아니다.
그래서 `spot_node_destroy()`는 끝난 것처럼 보이는데 `ctx_term()`은 계속
reaper `done`을 기다린다.

## 현재 판단

### 1. 가장 가능성 높은 원인
`core/src/services/common/service_runtime_base.hpp`의
`wait_drained()`와 `force_wait_remaining()`가 timeout 또는 중간 실패 시
미제거 socket을 내부 map으로 복구하지 않고 그대로 버린다.

문제가 되는 패턴은 이렇다.

- `wait_drained()`는 `_closing_sockets`를 `swap()`으로 꺼낸 뒤
  `ctx->wait_for_socket_removal()`을 돈다.
- 그 과정에서 하나라도 timeout 되면 즉시 `-1`을 반환한다.
- 그런데 그때 아직 제거되지 않은 socket들은 `_closing_sockets`로
  되돌아가지 않는다.
- 이후 abortive path가 다시 `force_wait_remaining()`을 호출해도,
  lifecycle 쪽에서는 이미 "추적 대상이 없다"고 보인다.
- 반면 `ctx_t`는 실제 `_sockets` 목록에서 그 socket들을 계속 들고 있으므로
  `ctx_term()`은 무기한 대기한다.

이 구조는 문서에 적힌 관찰과 매우 잘 맞는다.

- `shutdown=abortive reason=110 live_slots=0 attachments=0`
- 그런데도 종료가 안 됨

`live_slots`/`attachments`는 `spot_runtime_t`가 들고 있는 포인터/attachment 수만
보여준다. lifecycle tracker가 timeout 시 socket을 잃어버리면,
runtime 숫자는 0인데 `ctx`에는 socket이 남아 있는 상태가 가능하다.

### 2. 두 번째 원인
`core/src/services/spot/spot_node.cpp`의 `spot_node_t::destroy()`가
abortive path 이후에도 실패를 사실상 성공으로 바꿔 버린다.

현재 흐름은 이렇다.

- graceful drain 실패 또는 잔여 slot/attachment가 있으면 abortive path 진입
- `_runtime->abortive_stop()`
- `_lifecycle.force_wait_remaining(5000)`
- `wait_owned_socket_removals(5000)`
- 그 뒤 `first_error = 0`
- 마지막에는 `graceful_error`가 남아 있어도 `return 0`

즉 teardown이 실제로 덜 끝났어도 API 표면에서는 성공으로 보인다.
이렇게 되면 실패가 `spot_node_destroy()`에서 바로 드러나지 않고
나중의 `ctx_term()` hang으로 밀려난다.

내 판단으로 이건 진단을 어렵게 만드는 구조다.

### 3. 세 번째 원인
문제가 TLS 전용은 아니다. 공통 비동기 close 경로가 race를 증폭시키고 있다.

특히 다음 경로들이 모두 `close_socket()` 기반의 비동기 close를 쓴다.

- attachment destroy:
  `core/src/services/spot/spot_node.cpp`
- monitor bridge close:
  `core/src/services/spot/spot_pub.cpp`
  `core/src/services/spot/spot_sub.cpp`
- data plane thread 종료:
  `core/src/services/spot/spot_data_plane.cpp`

여기서 monitor bridge는 `open_socket_monitor_bridge()`로 별도 PAIR socket을
context에 생성한다. split ctest 재현이 `tls_lock`가 아니라
`monitors` 케이스에서 먼저 터진 점을 보면, TLS/WS transport 자체가 단독 root
cause라기보다 "늦게 사라지는 socket이 하나라도 생기면 lifecycle tracker가
그걸 놓치는 구조"가 더 본질이라고 본다.

## 근거

### A. lifecycle tracker의 소실 가능성
`core/src/services/common/service_runtime_base.hpp`

- `close_socket()`은 socket을 `_closing_sockets`에 넣고 즉시 `stop/close`만 한다.
- `wait_drained()`는 `_closing_sockets`를 로컬 map으로 `swap()`해 간다.
- timeout 시 복구 없이 바로 `-1`을 반환한다.
- `force_wait_remaining()`도 같은 식으로 `owned/closing` map을 통째로
  `swap()`해 간다.

이 구현에서는 timeout 한 번이 "추적 포기"와 거의 같다.

### B. ctx는 끝까지 socket 존재를 기준으로 기다린다
`core/src/core/ctx.cpp`

- `wait_for_socket_removal()`은 `_sockets` 배열에서 포인터가 빠질 때까지
  기다린다.
- `terminate()`는 reaper의 `done`을 timeout 없이 기다린다.

즉 service layer가 socket 추적을 잃어버리면,
최종 hang은 자연스럽게 `ctx_term()`으로 옮겨간다.

### C. 현재 워크스페이스에서 본 재현 결과
2026-03-12 기준 로컬 확인 결과는 다음과 같았다.

- 단독 실행:
  `ZLINK_TEST_CASE=test_spot_tls_settings_lock_after_bind_connect_and_register`
  는 통과했고 `shutdown=graceful`만 보였다.
- 같은 바이너리의 순차 실행:
  `core/build/bin/test_spot_service_introspection`
  도 통과했다.
- 하지만 split ctest 실행:
  `ctest --output-on-failure --stop-on-failure -R '^test_spot_'`
  는 `test_spot_service_introspection_monitors`에서 60초 timeout이 났고,
  로그는 다음 형태였다.
  `service=spot ... shutdown=abortive reason=110 live_slots=0 attachments=0`

이 결과는 "특정 테스트 하나의 논리 버그"보다
"split/full 실행에서 더 잘 드러나는 lifecycle race"라는 해석을 뒷받침한다.

## 가장 가능성 높은 1순위 원인
1순위는 `service_runtime_base_t`의 timeout 후 추적 상실이다.

이 가설이 맞다면 아래 현상을 한 번에 설명한다.

- 단독 실행은 자주 통과
- split/full 순차 실행에서만 가끔 timeout
- abortive 로그는 찍히지만 종료는 끝나지 않음
- 남은 socket 패턴이 `spot` ctrl/pub-in/sub-out, monitor, transport endpoint로
  넓게 퍼짐

반대로 TLS teardown 자체만 원인이라면,
`monitors` 케이스 split timeout 재현까지 설명하기가 약하다.

## 구조적으로 맞는 해결책

### 1. lifecycle tracker를 "소실 불가" 구조로 바꿔야 한다
socket은 `ctx->destroy_socket()`으로 실제 제거가 확인되기 전까지
tracker에서 절대 사라지면 안 된다.

내가 권하는 기준은 다음이다.

- `owned -> closing -> removed` 상태 전이를 명시적으로 둔다.
- timeout 시 상태를 유지한다.
- 재시도/abortive 경로는 같은 socket 집합을 다시 볼 수 있어야 한다.

### 2. `spot_node_destroy()`는 teardown 미수렴을 성공으로 숨기면 안 된다
abortive path 이후에도 socket removal이 끝나지 않으면
`return -1`로 surface 해야 한다.

지금처럼 성공을 반환하고 `errno`만 남기는 방식은 테스트도 놓치기 쉽고,
실제 hang 지점을 `ctx_term()`으로 뒤로 미뤄 버린다.

### 3. `ctx_term()`의 bounded fallback은 마지막 단계여야 한다
`ctx`에 bounded fallback을 넣는 건 나쁘지 않다.
하지만 지금 단계에서 먼저 넣으면 상위 lifecycle bug를 덮을 위험이 있다.

순서는 다음이 맞다.

1. service-level tracker를 정확하게 고친다.
2. `spot_node_destroy()`가 실패를 정직하게 반환하게 한다.
3. 그래도 남는 rare hang에 대해 `ctx_term()` bounded fallback을 검토한다.

## 지금 코드에 바로 적용 가능한 최소 수정안

### 1. `service_runtime_base_t` 수정
최소 수정은 이쪽이다.

- `wait_drained()`에서 `swap()` 대신 snapshot만 뜨거나,
  timeout/실패 시 미제거 socket을 `_closing_sockets`로 되돌린다.
- `force_wait_remaining()`도 같은 원칙으로 바꾼다.
- `owned_socket_count()`와 별도로
  `owned_count / closing_count / last_timeout_socket_id` 정도를
  로그로 찍을 수 있게 한다.

이 수정만으로도 abortive 이후 "더 이상 정리할 대상이 없다"는
거짓 상태는 크게 줄어든다.

### 2. `spot_node_t::destroy()` 계약 수정

- abortive path 후 `graceful_error != 0`이면 `return -1`
- `first_error = 0`로 성공 처리하는 현재 흐름 제거
- shutdown log에 lifecycle tracker count도 같이 남김

그러면 hang이 `ctx_term()`까지 밀리기 전에
문제가 정확히 `spot_node_destroy()`에서 드러난다.

### 3. monitor bridge close 경로 점검
`monitors` 케이스가 split ctest에서 실제 timeout 난 만큼,
다음 경로는 우선순위를 높게 보고 같이 점검하는 게 맞다.

- `core/src/services/common/socket_monitor_bridge.hpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_sub.cpp`

특히 monitor socket close도 결국 lifecycle tracker 정확성에 기대고 있으므로,
tracker를 고치기 전에는 재현 양상이 계속 흔들릴 가능성이 높다.

## 장기적으로 더 나은 구조

### 1. lifecycle helper를 상태 기반으로 키우기
지금의 `owned_sockets`/`closing_sockets` 두 map만으로는
"언제 close를 걸었는지", "몇 번 timeout 났는지", "abortive 대상인지"를
설명하기 어렵다.

`socket_lifecycle_entry` 같은 구조로 아래를 들고 가는 편이 낫다.

- socket pointer
- socket id
- owner kind
- close started timestamp
- stop issued 여부
- abortive escalated 여부
- last wait errno

### 2. close 정책을 계층별로 구분

- attachment, monitor socket, control socket처럼 개수가 적고 ownership이 명확한 건
  가능한 한 `close_socket_and_wait()`에 가깝게 다룬다.
- worker/data plane 내부 fanout socket처럼 thread join과 묶여야 하는 건
  비동기 close를 유지하되 tracker가 절대 잃어버리지 않게 한다.

### 3. `ctx` fallback은 디버그 가능한 형태로
나중에 `ctx_term()` fallback을 넣더라도
그냥 강제 종료만 하지 말고 최소한 아래 정보는 남겨야 한다.

- 남아 있는 socket count
- socket type / endpoint
- service-level tracker에 잡혔는지 여부

그래야 상위 계층 누락과 transport 자체 문제를 구분한다.

## 테스트에서 꼭 보강해야 할 회귀

### 1. lifecycle tracker timeout 보존 테스트
`service_runtime_base_t`에 대해,
"wait timeout 이후에도 closing socket count가 줄지 않는다"는 회귀가 필요하다.

핵심 assertion은 이거다.

- timeout 전 `closing_count > 0`
- timeout 후에도 같은 socket이 tracker에 남아 있음
- 이후 실제 제거되면 그때만 count가 0이 됨

### 2. split 순차 회귀
현재 증상이 순차 실행에서만 잘 드러나므로,
다음 조합을 고정 회귀로 묶는 게 좋다.

- `monitors -> topology_summary -> tls_lock`
- 또는 현재 ctest split 구성을 그대로 재현하는 smoke sequence

### 3. destroy contract 회귀
abortive 이후에도 socket이 남아 있으면
`zlink_spot_node_destroy()`가 실패를 반환하는지 확인하는 테스트가 필요하다.

### 4. 실패 시 errno 보존
`topology_summary`의 registry NULL 재현이 다시 나오면
그때는 반드시 `zlink_errno()`를 assertion/로그로 남겨야 한다.

지금 정보만으로는 이것이

- 단순 `EADDRINUSE` 계열의 isolation 문제인지
- teardown leak의 2차 증상인지
- registry start 자체의 다른 버그인지

를 정확히 분리하기 어렵다.

## 정리
내 우선순위는 다음과 같다.

1. `service_runtime_base_t`의 timeout 후 추적 소실 수정
2. `spot_node_destroy()`의 성공/실패 계약 수정
3. monitor bridge 경로 포함 split 회귀 추가
4. 그 다음에 `ctx_term()` bounded fallback 검토

즉 지금은 `ctx`가 약해서 hang이 생긴다기보다,
그 전에 service-level lifecycle이 아직 "끝까지 책임지는 추적기"가 아니라고 본다.

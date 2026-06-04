# Spot Shutdown 구조 개선안

기준 상태:
- 1차 수정 완료 커밋: `73dfc80f`
- 남은 증상: `test_spot_*` 반복 실행에서 비결정적 timeout, `shutdown=abortive ... tracked=N` 로그, 드물게 `Assertion failed: _term_acks > 0`

관련 문서:
- `doc/plan/direct-callback-recv/bug/00-overview.ko.md`
- `doc/plan/direct-callback-recv/bug/01-wait-drained-socket-tracking-loss.ko.md`
- `doc/plan/direct-callback-recv/bug/02-async-mailbox-reaper-data-race.ko.md`
- `doc/plan/direct-callback-recv/bug/deterministic-lifecycle-root-cause-review.ko.md`
- `doc/plan/direct-callback-recv/codex-spot-shutdown-review.ko.md`
- `doc/plan/direct-callback-recv/claude-spot-shutdown-review-response.ko.md`
- `doc/plan/direct-callback-recv/claude-spot-shutdown-review-response-phase2.ko.md`

## 한 줄 결론

지금 문제는 두 층의 구조 문제가 겹친 상태다.

첫째, `BUG-02` 문서 기준으로 현재 남은 직접 원인은
**direct callback의 async mailbox와 reaper가 같은 socket 내부 상태를 동시에
건드릴 수 있는 core data race**다.

둘째, `deterministic lifecycle` 문서 기준으로는
**서비스별 destroy 계약과 ownership이 공통화되어 있지 않아**
같은 종류의 종료 결함이 다른 서비스와 다른 transport에서 반복해서 드러난다.

그래서 한쪽에서 timeout 방지를 위해 보완을 넣으면 다른 쪽에서
ack imbalance, late pipe completion, ctx hang, split/full-suite-only failure가
계속 다른 형태로 튀어 나온다.

따라서 다음 단계는 "문제 케이스 하나를 우회하는 patch"가 아니라,
**core의 단일 스레드 termination invariant를 복구하고
서비스 destroy 계약을 공통 lifecycle kernel로 정렬하는 구조 개선**이어야 한다.

## 왜 구조 개선이 필요한가

### 1. 같은 계열의 실패가 서로 다른 증상으로 나타난다

현재 관찰된 실패는 세 가지다.

- `wait_drained()` timeout 후 abortive 로그가 찍히지만 종료가 끝나지 않음
- `ctx_term()`이 reaper done을 기다리며 hang
- 드물게 `own.cpp`에서 `_term_acks > 0` assertion

이 셋은 서로 다른 버그처럼 보이지만 실제로는 공통 뿌리가 있다.

- shutdown 중 어떤 socket의 termination 완료 기준이 흔들림
- 그 결과 어떤 경우에는 "아직 남은 socket"을 lifecycle tracker가 놓침
- 어떤 경우에는 "이미 0인 ack"에 추가 unregister가 들어옴
- 어떤 경우에는 socket이 destroy readiness를 영원히 만족하지 못함

문제의 본질은 "특정 API 호출 한 줄"이라기보다
**종료 상태와 종료 완료 판단이 여러 레이어에 흩어져 있다는 점**이다.

다만 현재 시점의 직접 원인 우선순위는 분명하다.

- `BUG-01`: service-level tracking loss
- `BUG-02`: async mailbox vs reaper data race

표면 증상은 구조 문제 전체의 일부이지만
지금 남아 있는 1순위 코어 결함은 `BUG-02` 쪽으로 봐야 한다.

### 2. 현재 모델은 책임이 두 군데에 나뉘어 있다

#### spot 쪽 책임

`spot_node_t`, `spot_runtime_t`, `service_runtime_base_t`는
다음 역할을 직접 한다.

- owned socket 등록
- close 요청
- wait_drained / force_wait_remaining
- abortive escalation
- destroy 성공/실패 판단

말하자면 spot 레이어가 "내가 가진 socket이 다 사라졌는가"를 자체적으로 판단한다.

#### core 쪽 책임

반면 실제 socket 수명 종료는 `core`가 결정한다.

- `socket_base_t::process_term()`
- `pipe_t::terminate()`
- `pipe_t::process_pipe_term_ack()`
- `socket_base_t::pipe_terminated()`
- `ctx_t::destroy_socket()`
- reaper done 신호

실제 destroy 완료의 source of truth는 core인데,
spot이 별도 추적 집합으로 "사실상 끝났다"고 먼저 판단하는 순간
두 상태가 어긋날 수 있다.

### 3. 하나를 고치면 다른 문제가 튀는 이유

1차 수정으로 `wait_drained()` timeout 후 socket tracking 유실은 막았다.
그 결과 `tracked=` 로그가 남고, 이전처럼 tracker가 socket을 완전히 잃지는 않는다.

하지만 그 다음엔 `_term_acks` assertion과 잔여 teardown race가 더 선명하게 드러났다.
이건 "새 버그가 생겼다"기보다
원래 있던 더 깊은 shutdown 불일치가 위로 올라온 것이다.

지금 보이는 패턴은 이렇다.

1. 표면 버그 수정
2. deeper invariant failure 노출
3. 또 다른 patch
4. 다른 종료 경로에서 새 증상 노출

이 루프를 끊으려면 patch target을 spot 개별 call site가 아니라
termination model 자체로 올려야 한다.

## 현재 root cause를 어떻게 다시 정의할 것인가

지금 이슈를 가장 정확하게 다시 정의하면 아래와 같다.

### 1. 1차 문제는 이미 분리됨

`service_runtime_base_t`의 tracking loss는 실제 버그였고,
이미 수정 대상으로 식별되었다.

하지만 그 수정은 "남은 socket을 안 보이는 상태로 만들지 않게" 한 것이지
socket termination 자체를 안정화한 것은 아니다.

### 2. 현재 남은 직접 원인은 async mailbox vs reaper data race

`BUG-02` 문서 기준으로 현재 남은 1순위는
`socket_base_t`의 async mailbox 처리와 reaper handoff가
서로 완전히 배타적이지 않다는 점이다.

구체적으로는 이렇다.

- direct callback 활성 소켓은 mailbox 명령을 I/O 스레드의
  `process_async_mailbox()`에서 처리한다
- `stop_async_mailbox_processing()`은 I/O 스레드 완료를 기다리지 않고 즉시 반환한다
- 그 상태에서 `close()` → `send_reap()` → `start_reaping()`이 진행될 수 있다
- 결과적으로 I/O 스레드와 reaper 스레드가 같은 socket의 `_pipes`,
  `_terminating`, `_term_acks`를 동시에 만질 수 있다

이건 단순한 종료 순서 문제를 넘어,
**socket core가 전제하던 "한 시점에 한 스레드만 socket 내부 상태를 만진다"는
기본 가정이 깨진 상태**다.

### 3. ack/accounting 불일치는 그 race의 결과로 보는 게 맞다

남은 문제의 핵심은 다음 두 조건 중 하나다.

- register되지 않은 termination completion이 나중에 unregister 경로로 들어온다
- 같은 termination event 계열이 중복 completion으로 처리된다

이 불일치가 생기면 결과는 둘 중 하나다.

- underflow 쪽이면 `_term_acks > 0` assertion
- leak 쪽이면 destroy readiness가 오지 않아 socket removal timeout

말하자면 timeout과 assertion은 다른 문제라기보다
같은 구조 불일치의 두 방향이다.

중요한 점은, 현재는 이것을 "독립적인 pipe ack 모델 결함"으로 단정하기보다
**I/O 스레드와 reaper의 동시 접근이 만든 accounting 오염 결과**로 보는 쪽이
다른 문서들과 더 잘 맞는다.

### 4. 서비스 destroy 계약 분산은 race를 반복해서 surface시키는 상위 원인이다

`deterministic lifecycle` 문서가 지적한 부분도 중요하다.

- 누가 stop 하는가
- 누가 close 하는가
- 누가 wait 하는가
- 누가 destroy 성공을 판정하는가

이 네 가지가 서비스마다 다르고, 같은 서비스 안에서도 handle/runtime/worker/ctx에
분산되어 있다.

이 구조에서는 core 쪽 race 하나를 줄여도
다른 서비스나 다른 teardown sequence가 다음 종료 결함을 다시 surface시킨다.

그래서 현재 문제는 두 층으로 정리해야 한다.

- 하위 직접 원인: async mailbox vs reaper data race
- 상위 구조 원인: 공통 lifecycle contract 부재와 ownership 분산

### 5. term_endpoint는 1차 원인보다 "경합 증폭기"에 가깝다

phase2 문서의 포인트는 중요하다.

- attachment destroy 직전에 `term_endpoint()`를 부름
- inproc 경로는 `erase_pipes()`를 타며 `terminate(true)`를 사용
- peer pipe는 `waiting_for_delimiter` 상태로 갈 수 있음

이 흐름은 분명 teardown window를 넓힌다.

다만 코드 수준에서 보면 `term_endpoint()` 하나만으로
`_term_acks` underflow를 직접 증명하기는 어렵다.
정상 상태기계라면 여전히 각 pipe는 최종적으로 한 번만 completion을
알려야 하기 때문이다.

따라서 현 단계의 판단은 이렇다.

- `term_endpoint()` 선호출은 제거 후보가 맞다
- 하지만 그것만 제거해도 근본 문제가 끝난다고 보긴 어렵다
- 더 직접적인 본질은 async mailbox와 reaper의 단일 스레드 가정 위반이다

## 구조적으로 바꿔야 할 목표

구조 개선의 목표는 네 가지다.

### 목표 1. reaper handoff 전 async mailbox quiesce를 강제

close 이후 reaper가 들어오기 전에,
해당 socket의 async mailbox 처리가 완전히 빠져나왔다는 보장이 필요하다.

`close()`나 `start_reaping()` 경계에서 다음이 성립해야 한다.

- async handler가 더 이상 `process_commands()`를 실행하지 않음
- mailbox io_context가 분리됨
- 이후부터는 reaper만 socket 내부 termination 상태를 만짐

이 보장이 없으면 이후의 accounting/ownership 정리는 모두 불안정하다.

### 목표 2. 서비스 destroy 계약을 공통 lifecycle contract로 고정

서비스별로 다음 순서를 동일 의미로 강제해야 한다.

1. 새 work 유입 차단
2. observer / task detach
3. worker stop signal
4. worker join 완료
5. owned socket final close
6. drain 확인
7. 성공 시에만 destroy 반환

이 계약이 공통화되지 않으면, 같은 클래스의 종료 결함이 다음 서비스로 계속
이동할 가능성이 높다.

### 목표 3. teardown 완료 기준을 core 하나로 통일

spot 같은 서비스는 "무엇을 언제 멈출지"만 결정하고,
실제 socket/pipe 제거 완료는 core termination graph가 판정해야 한다.

spot은 core가 "이 socket은 제거되었다"고 말할 때까지만 기다려야 한다.

### 목표 4. destroy 실패를 숨기지 않기

abortive는 "정리 시도"이지 "성공 인정"이 아니다.
graceful이든 abortive든 끝까지 수렴하지 못했으면 destroy는 실패를
반환해야 한다.

그렇지 않으면 teardown fault가 항상 `ctx_term()` hang으로 뒤로 밀린다.

## 구체적인 구조 개선 방향

### 1. core: async mailbox quiesce barrier를 먼저 넣어야 한다

다른 문서들을 반영하면, 가장 먼저 해야 할 구조 수정은
`close()` 전에 async mailbox가 완전히 빠져나오도록 보장하는 것이다.

핵심 요구사항은 아래와 같다.

- `stop_async_mailbox_processing()`이 단순 flag set에서 끝나면 안 됨
- async handler 종료를 기다리는 bounded wait가 필요함
- `start_reaping()`은 이전 async handler가 남아 있으면 진입하면 안 됨
- reaper handoff 이후에는 socket 내부 상태를 reaper만 만져야 함

termination 모델 개편보다 먼저
**core의 단일 스레드 접근 invariant를 복구**해야 한다.

### 2. core: own_t와 socket_base_t의 역할 분리를 재검토

이전 버전 문서에서는 `own_t::_term_acks`와 pipe termination 상태를
아예 분리하는 쪽을 1순위로 썼다.

다른 문서를 반영하면 이건 이렇게 정리하는 편이 더 맞다.

- 1차: async mailbox/reaper 동시 접근 제거
- 2차: 그 이후에도 accounting ambiguity가 남으면 pipe state를 socket-local
  상태로 분리

pipe ack/state 분리는 여전히 유력한 구조 개선안이지만,
**data race 제거 이후에 적용 여부를 확정할 2차 core 리팩터링**으로 두는 편이
안전하다.

#### 2차 리팩터링 후보

`own_t::_term_acks`는 다음만 담당하게 제한한다.

- `term_child()` / `process_term_req()` 계열
- owned object의 `term_ack`
- 기타 pipe 외의 owned lifecycle ack

필요 시 pipe termination은 더 이상 `own_t::unregister_term_ack()` 경로를
타지 않게 하고, socket-local pending state로 빼는 방향을 검토한다.

`socket_base_t`는 별도 상태를 가져야 한다.

예시:

- `terminating_pipes`
- `pending_pipe_terms`
- `socket_term_started`

핵심은 이 상태가 "현재 socket destroy를 막고 있는 pipe completion"의
source of truth가 되는 것이다.

### 3. core: process_term은 quiesced 상태에서만 snapshot을 고정

`socket_base_t::process_term()`은 다음 순서로 바뀌는 것이 맞다.

1. 더 이상 새 endpoint를 받지 않도록 endpoint unregister
2. 현재 live pipe를 termination snapshot으로 고정
3. snapshot에 포함된 pipe만 pending_pipe_terms로 카운트
4. 각 pipe에 terminate 발행
5. owned-object termination은 그 다음 진행

이렇게 해야 "무엇을 기다리고 있는지"가 먼저 고정된다.
지금처럼 live `_pipes.size()`를 보고 generic ack를 올리는 방식은
late event와 섞일 때 의미가 흐려진다.
다만 이 정리는 반드시 async mailbox quiesce 이후 단일 스레드 상태에서
이뤄져야 한다.

### 4. core: pipe_terminated는 단일 소유 스레드에서만 상태를 갱신

`socket_base_t::pipe_terminated()`는 다음 역할만 해야 한다.

- live `_pipes`에서 제거
- `terminating_pipes`에 있으면 그것도 제거
- `pending_pipe_terms` 감소
- socket destroy readiness 재평가

중요한 점은 이 함수가 async mailbox와 reaper 사이에서 경쟁하지 않게 하는 것이다.
그 다음 단계에서 필요하면 generic `own_t::_term_acks`와 분리한다.

### 5. core: late attach도 같은 bookkeeping을 타게 함

terminating 상태에서 새 pipe가 붙는 경우를 별도 예외로 두면 안 된다.
late attach는 다음 둘 중 하나로 명확히 처리해야 한다.

- 즉시 reject + terminate
- 아니면 terminating snapshot에 즉시 편입

핵심은 late attach가 destroy readiness 조건을 우회하지 못하게 하는 것이다.

이 포인트는 `ctx_t::terminate()`의 pending inproc bind 보정 경로와도 연결된다.
종료 중에 late bind가 들어와도 bookkeeping이 빠지지 않아야 한다.

### 6. service_runtime_base_t는 helper 축소가 아니라 lifecycle kernel로 승격

기존 문서 초안에서는 `service_runtime_base_t`를 observer/helper 수준으로
축소하는 쪽으로 적었지만, `deterministic lifecycle` 문서와 맞춰 보면
그 표현은 수정하는 편이 맞다.

더 적절한 방향은 다음이다.

- socket removal 완료의 source of truth는 여전히 core
- 하지만 서비스 레벨의 state machine은 `service_runtime_base_t` 같은
  공통 kernel이 소유
- worker/task/observer/socket ownership과 stop/join/close/wait 순서를
  여기서 공통 계약으로 강제

`service_runtime_base_t`는 단순 socket helper가 아니라
**서비스 공통 lifecycle kernel**로 키우되,
socket final removal 자체는 core 판단을 따르게 해야 한다.

### 7. spot: handle destroy에서 선행 term_endpoint 제거

node-owned inproc attachment handle에 대해서는
destroy 직전의 `term_endpoint()` 선호출을 제거하는 방향이 맞다.

이유는 두 가지다.

- 어차피 socket close가 core termination graph를 타며 pipe를 정리한다
- handle이 peer internal socket보다 먼저 pipe 상태를 건드리면
  data-plane 쪽 teardown window가 불필요하게 넓어진다

attachment destroy는 다음 역할만 하면 된다.

- registry/unregister/monitor 정리
- attachment socket close 요청

pipe 종료 순서 자체는 core가 책임진다.

### 8. spot: node destroy 순서를 quiesce-first로 재정렬

현재보다 구조적으로 맞는 순서는 아래와 같다.

1. 새 작업 유입 중단
2. peer disconnect / unbind 요청
3. data-plane/control quiesce 및 join
4. residual handle / attachment sweep
5. core socket removal wait
6. 실패 시 error 반환

핵심은 "peer internal socket이 아직 active한 상태에서 attachment 쪽 pipe를 먼저
흔들지 않는다"는 것이다.

`spot`은 공통 lifecycle kernel이 정한 순서를 따르고
core removal wait를 통해서만 종료 완료를 확인해야 한다.

## destroy 정책

구조 개선의 destroy 정책은 **strict fail-fast**가 맞다.

### 의미

- teardown mismatch가 있으면 즉시 failure surface
- 성공처럼 숨기지 않음
- 무한 대기 대신 bounded wait 후 failure 반환 가능
- cleanup은 하되 결과는 실패로 남김

### 하지 말아야 할 것

- abortive 이후 `first_error = 0` 같은 성공 위장
- 일부 teardown fault를 내부에서 흡수하고 API는 성공처럼 반환
- ctx hang을 막는다는 이유로 상위 lifecycle bug를 계속 숨김

## 구현 단계 제안

### 단계 1. async mailbox quiesce barrier 도입

우선순위가 가장 높다.

- `close()` 전에 async mailbox 중지 + 완료 대기
- `start_reaping()` 진입 전 async handler 잔존 여부 방어
- direct callback 소켓이 reaper와 동시에 `_pipes`를 건드리지 않게 보장

이 단계가 끝나야 나머지 termination/accounting 논의가 의미가 생긴다.

### 단계 2. 서비스 공통 lifecycle contract 정리

- `service_runtime_base_t`를 서비스 공통 lifecycle kernel로 승격
- worker/task/socket/observer ownership 등록 및 stop/join/close/wait 순서 공통화
- destroy 성공 계약을 "실제 drain 완료"로 고정

### 단계 3. spot teardown 단순화

- handle destroy에서 term_endpoint 제거
- node destroy 순서를 quiesce-first로 재정렬
- abortive success masking 제거

이 단계는 core가 source of truth가 된 뒤에 넣어야 안정적이다.

### 단계 4. termination accounting 재검토

- quiesce barrier 이후에도 `_term_acks` ambiguity가 남는지 재평가
- 남아 있으면 pipe pending state를 socket-local로 분리
- late attach / duplicate completion guard 강화

### 단계 5. 진단/로그 표준화

- socket id
- socket type
- async mailbox active/quiesced 상태
- owned ack count
- pending pipe count
- live pipe count
- terminating state

이 정보를 failure path에 표준적으로 남기면
다음 teardown 이슈도 지금보다 훨씬 빨리 좁힐 수 있다.

## 테스트 전략

### 1. core 단위 테스트

반드시 필요한 시나리오:

- async mailbox active 상태에서 close 진입
- async handler 종료 직후 reaper handoff
- terminating 직전 pipe ack 도착
- terminating 직후 pipe ack 도착
- late attach during termination
- duplicate completion guard
- pending inproc bind during ctx termination

이 테스트는 "underflow가 없어야 한다"보다
"destroy readiness가 언제 true가 되는지"를 검증하는 쪽이 더 중요하다.

### 2. spot 기능 테스트

최소 대상:

- `explicit_handles`
- `register_null`
- `monitors`
- `tls_lock`
- direct callback 활성/비활성 destroy 경로 비교

그리고 destroy 순서가 다른 케이스를 따로 봐야 한다.

- handle 먼저 destroy 후 node destroy
- node destroy가 residual handle까지 sweep
- monitor bridge 활성 상태
- discovery/registry 연결 상태

### 3. 반복 실행 게이트

단발 통과로는 부족하다.
최소한 split ctest 반복 게이트가 필요하다.

예시:

- `ctest --output-on-failure --stop-on-failure -R '^test_spot_'`
- 동일 명령 20회 반복
- timeout 0건
- `_term_acks` assertion 0건
- abortive-success masking 0건

## 기대 효과

이 구조 개선이 들어가면 얻는 효과는 아래와 같다.

### 1. timeout과 assertion을 같은 모델로 설명 가능

지금은 timeout과 assertion이 각각 별도 대응을 요구하는 것처럼 보이지만,
구조 개선 후에는 둘 다
"async mailbox/reaper handoff 실패 또는 그 이후 bookkeeping 불일치"라는
같은 모델 안에서 다룰 수 있다.

### 2. spot 수정이 core invariant를 더 이상 가리지 않음

spot이 별도 추적기로 성공을 판단하지 않게 되면,
문제가 생겨도 정확히 core invariant failure로 드러난다.
이건 디버깅 비용을 크게 줄인다.

### 3. 새 teardown 버그가 나와도 진입점이 명확해짐

앞으로 비슷한 문제가 다시 생겨도
"spot patch를 더 넣을까?"가 아니라
"async mailbox handoff가 보장됐는가, 그 다음 pending state가 어디서 어긋났는가?"
로 바로 들어갈 수 있다.

## 최종 판단

현재 상태에서 가장 위험한 선택은
"재현되는 테스트 하나를 기준으로 spot call site 몇 군데만 계속 고치는 것"이다.

그 접근은 단기적으로는 timeout 빈도를 낮출 수 있지만,
다음 경로에서 다른 teardown race를 다시 드러낼 가능성이 높다.

지금 필요한 것은 다음 방향의 구조 개선이다.

- core에서 async mailbox와 reaper의 단일 스레드 invariant를 복구하고
- 서비스 공통 lifecycle kernel로 destroy 계약을 고정하고
- 필요 시 pipe termination state를 generic own ack에서 분리하고
- spot은 quiesce 순서와 close orchestration만 담당하게 단순화하고
- destroy는 strict fail-fast로 고정하는 것

이 방향이 맞으면, 이후 개별 patch는 더 이상 땜질이 아니라
명확한 모델 위의 구현 정리 작업이 된다.

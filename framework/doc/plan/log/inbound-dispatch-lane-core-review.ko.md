# Core clean review — byte HWM과 Application·Completion pair

이 문서는 [inbound dispatch lane 설계](../inbound-dispatch-lane-design.ko.md) §8.2의 2단계
Core clean review 기록이다. Round마다 candidate, reviewer, finding과 처리 결과를 남긴다.
판정 규칙은 설계 문서 §8.2를 따른다. 같은 candidate에서 두 reviewer가 모두 `Medium` 이상
finding을 0건으로 보고할 때만 `CLEAN`이다.

두 reviewer에게 주는 공통 brief는
[core review prompt](inbound-dispatch-lane-core-review-prompt.md)다. Round마다 candidate
SHA만 바꿔서 그대로 사용한다.

## Round 1

### 검토 입력 (CR-01)

| 항목 | 값 |
| --- | --- |
| Candidate commit | `d7d682bb1f` |
| 비교 기준 commit | `8bc2aa6786` (count 기반 HWM 의미) |
| Candidate worktree | `/tmp/zlink-core-candidate-d7d682bb1f` (읽기 전용) |
| 전체 diff | `git diff 8bc2aa6786..d7d682bb1f -- core bindings/c` (128 file, +4,086 / −2,534) |
| 공통 review 입력 | 설계 문서, `AGENTS.md`, spec 작성 지침, source comment 원칙, software design 원칙, `core/doc` Core 정식 spec, C public header, Core source·test·benchmark·monitoring, 위 diff, [reqrep multipart rollback review](inbound-dispatch-lane-reqrep-multipart-rollback-review.ko.md) §9 |
| 공통 prompt | [core review prompt](inbound-dispatch-lane-core-review-prompt.md) rubric v1 |

두 reviewer에게 같은 prompt와 같은 candidate를 주고, 한 reviewer의 결과를 다른 reviewer에게
제공하지 않았다. Candidate를 별도 worktree로 고정했으므로 review 중 main 작업 tree의 변경이
검토 대상에 섞이지 않는다.

Candidate에 포함된 stage 1 수정 이력은 다음과 같다.

| commit | 내용 |
| --- | --- |
| `563e11d614` | reply submit의 completion credit 회복, lb·DEALER·dist multipart rollback |
| `58aa55df8b` | multipart byte admission을 per-call HWM writer로 한정 |
| `0830b29317` | inproc transport pair readiness 발행과 pair readiness key 정리 |
| `af2ef1e558` | perf fixture reply retry를 측정 경로 밖으로 이동 |
| `d7d682bb1f` | memory amplification 하네스와 C-07 증거 기록 |

### 실행 기록 (CR-02, CR-03)

| Reviewer | Model | Reasoning | 실행 시각 | 판정 |
| --- | --- | --- | --- | --- |
| Codex 5.6 High | `gpt-5.6-sol` | high | 2026-07-30 11:57 | `NOT CLEAN` |
| Claude Fable | `claude-fable-5` | interleaved extended thinking | 2026-07-30 12:17 | `NOT CLEAN` |

Codex는 read-only sandbox로 실행했으므로 지정한 경로에 report file을 쓰지 못하고 stdout으로
보고했다. 아래 finding은 그 보고서를 옮긴 것이다. 다음 round에서는 reviewer가 report를 쓸 수
있는 경로를 sandbox 밖으로 지정한다.

### Finding (CR-04)

| ID | Reviewer | Severity | Category | 위치 | 요지 |
| --- | --- | --- | --- | --- | --- |
| CR1-01 | Codex | `Critical` | contract | `bindings/c/include/zlink/eventing/api.h:42` | C binding monitor header가 Core v2 monitoring ABI와 불일치한다 |
| CR1-02 | Codex | `Critical` | contract | `core/src/api/socket/request_completion_queue_internal.hpp:25` | timeout·cancellation completion control deque에 상한이 없다 |
| CR1-03 | Codex | `High` | contract | `core/src/runtime/core/pipe.cpp:1271` | 빈 pipe의 oversize 예외가 `ZLINK_OPT_MAXMSGSIZE`를 확인하지 않는다 |
| CR1-04 | Codex | `Medium` | leftover | `core/doc/guide/12-socket-options.ko.md:16` | 공개 guide가 HWM을 message 개수 제한으로 설명한다 |
| CR1-05 | Codex | `Medium` | contract | `core/tests/integration/test_zmp_request_reply.cpp:928` | pair identity·generation wire fixture가 없다 |

#### CR1-01 — C binding monitor header가 Core v2 ABI와 불일치

- 근거: `bindings/c/include/zlink/eventing/api.h`는 version 없는 옛 `zlink_monitor_status_t`를
  그대로 선언하고 applied·deferred HWM field가 32-bit count 시절 형태다(98~123행). Core
  header는 `abi_version`과 `struct_size`로 시작하고 v2 64-bit byte field를
  갖는다(`core/include/zlink/eventing/api.h:27-158`). `zlink_c` target은 Core runtime을
  link하면서 `bindings/c/include`를 노출하고(`bindings/c/CMakeLists.txt:78-82`), perf target도
  그 include tree로 compile한다(`bindings/c/perf/CMakeLists.txt:14,43-46`). Runtime은 호출자
  크기를 받지 않고 `memset(out_, 0, sizeof(*out_))` 후 v2 layout을
  쓴다(`core/src/runtime/sockets/common/socket_base_monitor.cpp:37-109`).
- 위반: 설계 문서 §7 항목 8, §8.1 항목 3·9. 하나의 C ABI를 두 header tree에서 유지하므로
  information hiding도 깨진다.
- 영향: candidate의 C binding header로 compile한 소비자는 더 작은 struct를 잡는데 runtime은 더
  큰 v2 struct를 쓴다. ABI 파손이고 호출자 memory를 넘어 쓰는 out-of-bounds write다.
- 대안 A: `bindings/c/include/zlink/eventing/api.h`를 Core v2 layout과 동기화하고 크기·offset·
  version·field type을 compile-time assert로 고정한다.
- 대안 B: C ABI header를 두 번 유지하지 않는다. `core/include`에서 C binding include tree를
  생성하거나 packaging하고 불일치를 build에서 거부한다.
- 권장: A로 이 candidate를 고치고 이어서 B를 채택한다. A는 가장 작은 안전한 수정이고, B는
  호출자 부담이나 runtime 비용 없이 향후 drift를 없앤다.

#### CR1-02 — completion control deque에 상한이 없다

- 근거: `queue_state_t`가 capacity나 예약 counter 없이 `std::deque<control_t> controls`를
  소유하고 `enqueue()`는 조건 없이 `push_back()` 후 notification을
  post한다(`request_completion_queue_internal.cpp:47-75`). Request admission도 상한 없는 map에
  넣는다(`socket_request_reply_internal.hpp:59-71,147-156`). Timeout마다 control record가
  들어가고(`socket_request_reply_internal.cpp:128-173`) peer disconnect는 pending request마다 한
  건씩 넣을 수 있다(`socket_request_reply_dispatch.cpp:150-189`). 비우는 쪽은 owner drain
  하나뿐이다(`request_completion_queue_internal.cpp:90-109`).
- 위반: 설계 문서 §4.9 6단계는 payload 없는 timeout·cancellation completion command를
  **유계** control queue로만 허용한다.
- 영향: completion owner가 멈춘 동안 다른 thread가 계속 request를 보내 timeout·disconnect가
  발생하면 deque와 per-item notification이 무제한으로 늘어난다. 공개 request 경로의 무계
  memory 증가다.
- 대안 A: request를 admit할 때 control slot 하나를 예약하고, timeout·cancellation·disconnect가
  그 예약을 ready record로 바꾼다. 예약이 없으면 보내기 전에 거부하거나 backpressure한다.
- 대안 B: deque를 없애고 종료 상태를 유계 pending request table에 두고, owner wakeup을 묶어
  보낸 뒤 owner가 ready record를 drain한다.
- 권장: A. 예약은 scan 없이 exactly-once callback을 유지하고 overload를 소유권 이전 전에
  드러내며 completion 경로를 O(1)로 유지한다.

#### CR1-03 — 빈 pipe oversize 예외가 최대 message 크기를 확인하지 않는다

- 근거: `can_commit_bytes_unlocked()`는 HWM이 무제한이면 모든 write를 허용하고
  `_msgs_written <= _peers_msgs_read`이면 크기와 무관하게 허용한다. 최대 message 크기를 전혀
  확인하지 않고 `pipe_t`에도 그 값이 없다(`pipe.hpp:269-322`). `maxmsgsize`는 socket option이
  소유하며 주로 network decoder가 쓴다(`options.hpp:163-167`). 정식 socket spec은 oversize 한
  건도 `ZLINK_OPT_MAXMSGSIZE`를 만족해야 한다고 적는다(`core/doc/spec/core/socket/README.ko.md:438-443`).
- 위반: 설계 문서 §6.3, §8.1 항목 5.
- 영향: 유한한 HWM이 첫 message를 제한하지 못한다. inproc은 decoder를 지나지 않으므로 과대
  logical message가 pipe에 남아 연결당 memory 불변식을 깬다.
- 대안 A: pipe pair 생성·갱신 시 유효 최대 accounted 과금을 계산해 `pipe_t`에 두고 multipart
  누적과 commit 전에 강제한다.
- 대안 B: 모든 socket send adapter가 route 선택 전에 완성된 logical message를 검사하고 inproc과
  network receive 경로에 같은 검사를 따로 둔다.
- 권장: A. `pipe_t`가 이미 모든 transport의 byte accounting과 multipart commit을 소유하므로 한
  곳의 한 값이 더 단순하고 일반적이다.

#### CR1-04 — 공개 guide가 count 의미를 유지한다

- 근거: `core/doc/guide/12-socket-options.ko.md:16-19`는 두 HWM이 queue message slot을
  제한한다고 적고, glossary는 HWM을 message 개수 제한으로
  정의한다(`core/doc/guide/glossary.ko.md:7`). 성능 guide도 slot 계약을
  반복한다(`core/doc/guide/10-performance.ko.md:8-12`). 영문 mirror도 count
  기준이다(`12-socket-options.md:17-20`, `10-performance.md:11-14`).
- 위반: 설계 문서 §5.2, §6.6, §8.1 항목 10. `AGENTS.md`는 `doc/guide/`를 사용자용 의도·사용
  기준 문서로 정의한다.
- 영향: 사용자가 `1000`을 message 1,000건으로 기대하고 넣으면 runtime은 1,000 byte로 해석해
  훨씬 이르게 backpressure한다. 다음 bindings 단계의 breaking migration이 위험해진다.
- 대안 A: 다섯 위치를 accounted byte, 8-byte 값 형식, `0=무제한`, byte 기준 Auto 계획으로
  전부 갱신한다.
- 대안 B: guide에는 byte 기준 사용 지침만 간결히 남기고 정확한 형식·기본값·계산식·오류는 정식
  socket spec으로 연결한다.
- 권장: B에 glossary 한 문장 수정을 더한다. 결정 지점에서 단위를 주고 breaking contract 상세는
  한 문서가 소유한다.

#### CR1-05 — pair identity·generation wire fixture가 없다

- 근거: candidate의 reconnect case는 일반 application message 두 건을 보내고 공개 disconnect
  API를 호출한 뒤 receive가 비었는지 확인하고 다시
  연결한다(`test_zmp_request_reply.cpp:928-954`). Peer identity·pair id·lane·generation이 어긋난
  READY metadata를 만들지 않고, 한 lane만 끊거나 이전 generation의 reply·control frame을 주입하지
  않는다. `core/tests` 전체에서 `Zlink-Pair-Id`, `Zlink-Pair-Generation`, `pair_id`,
  `pair_generation` 관련 fixture를 찾지 못했다.
- 위반: 설계 문서 §7 항목 12, §8.1 항목 9와 1단계 완료 조건.
- 영향: C-05를 완료로 선언했지만 새 wire·lifecycle 불변식을 지키는 실행 gate가 없다. Lane을
  잘못 짝짓거나 한 lane만 살아 있거나 옛 generation reply를 받아들이는 회귀가 bindings 단계까지
  드러나지 않을 수 있다.
- 대안 A: `test_zmp_request_reply.cpp`에 raw ZMP peer를 추가해 정상·비정상 READY metadata와 두
  lane 순서, reconnect 주입을 다룬다.
- 대안 B: `test_transport_pair_protocol` wire fixture를 따로 만들어 metadata 불일치, 두 connect
  순서, 한 lane 실패, pair 전체 종료, reconnect 후 stale reply·control을 matrix로 다룬다.
- 권장: B. Protocol lifecycle matrix가 크므로 독립 fixture가 상위 request/reply test의 가독성을
  지키고 각 실패 불변식을 따로 진단할 수 있게 한다.

#### Codex가 확인하고 문제 없다고 본 항목

8-byte HWM option 검증과 4-byte 거부, `0=무제한`, byte 기준 Auto 계획, Core header의 v2 field,
payload+`msg_t`의 O(1) 과금, receive credit·LWM·multipart commit·rollback에 같은 과금 적용, pair
metadata 파싱, pair 전체 disconnect 처리, completion pipe에서 callback으로의 직접 전달,
`internal_pair_queue_internal.*` 제거를 확인했다. Hot path에서 `Medium` 이상 근거는 찾지
못했다. Benchmark·Valgrind 수치는 재실행하지 않고 기록된 증거만 확인했다.

### Claude Fable 추가 finding

Claude Fable은 같은 candidate와 비교 기준을 전체 검토하여 `High` 3건, `Medium` 4건,
`Low` 6건을 보고했다. Codex와 중복된 빈 pipe oversize finding은 CR1-03에 통합했다.

| ID | Severity | Category | 위치 | 요지 |
| --- | --- | --- | --- | --- |
| CR1-F01 | `High` | lifecycle | `core/src/runtime/sockets/common/socket_base_endpoint.cpp:330,409` | 명시적 disconnect가 Completion session을 endpoint map에 남겨 재연결을 반복한다 |
| CR1-F02 | `High` | concurrency | `core/src/runtime/sockets/common/socket_base_api.cpp:113,452,586` | `_transport_pairs` map과 Completion pipe drain의 동기화 owner가 없다 |
| CR1-F03 | `High` | contract | `core/src/runtime/sockets/common/socket_base_api.cpp:312,447` | reply callback이 completion wait owner가 아닌 임의 API·I/O thread에서 실행될 수 있다 |
| CR1-F04 | `Medium` | contract | `core/src/api/core/zlink.cpp:253` | `zlink_poller_modify()`가 spec과 달리 `ZLINK_POLLCOMPLETION`을 허용한다 |
| CR1-F05 | `Medium` | migration | `core/src/api/monitoring/monitor_socket_api.cpp:86` | 내부 monitor HWM 4,096 messages를 4,096 bytes로 조용히 재해석했다 |
| CR1-F06 | `Medium` | contract | `core/src/api/core/context_api.cpp:162` | 64-bit get 구현은 exact size를 요구하지만 spec은 capacity 의미로 설명한다 |
| CR1-F07 | `Medium` | contract | `core/tests/integration/test_zmp_request_reply.cpp` | paired endpoint disconnect와 reconnect 후 route 유지 회귀가 부족하다 |

#### CR1-F01 — 명시적 disconnect 뒤 Completion session이 재연결된다

- 근거: 두 lane을 `endpoint`와 `endpoint#completion`이라는 서로 다른 key로 등록했다.
  `zlink_disconnect(endpoint)`는 Application lane만 제거하므로 Completion session이 peer
  종료를 transport failure로 처리하고 재연결한다.
- 위반: 설계 §4.9와 §8.1은 lane 하나를 명시적으로 종료하면 pair 전체를 종료하도록 정한다.
- 대안 A: disconnect 경로가 두 key를 모두 찾아 shared pair reconnect를 비활성화한다.
- 대안 B: 두 lane을 endpoint multimap의 같은 key로 등록하고 pair state에서 reconnect
  허용 여부를 함께 관리한다.
- 선택: B를 적용한다. Endpoint 연산마다 `#completion` 예외를 반복하지 않고 기존
  endpoint 범위 연산이 pair 전체에 적용된다.

#### CR1-F02 — pair table과 Completion pipe drain에 동기화 owner가 없다

- 근거: command를 처리하는 thread가 `_transport_pairs`를 변경하는 동안 reply submit
  thread가 같은 `std::map`을 조회할 수 있다. Completion pipe도 여러 command 처리 thread가
  동시에 읽을 수 있다.
- 위반: pair lifecycle과 reply completion invariant의 owner가 분명해야 한다는 §8.2 검토
  기준과 POSD 정보 은닉 원칙을 위반한다.
- 대안 A: pair registry가 map, transition과 drain claim을 mutex 하나로 관리한다.
- 대안 B: 기존 socket dispatch lock으로 모든 table 접근과 drain을 보호한다.
- 선택: 현재 단계에서는 pair table 전용 mutex와 per-pair drain claim을 적용한다. Reply
  handler는 table lock 밖에서 실행하여 user callback 재진입과 table invariant를 분리한다.
  Registry 추출은 동작과 측정값을 바꾸는 후속 구조 개선으로 남긴다.

#### CR1-F03 — reply callback 실행 thread 계약이 지켜지지 않는다

- 근거: Completion pipe의 `read_activated()`가 실행되는 모든 command 처리 thread에서
  callback을 실행할 수 있었다. `get_events()`도 요청 event mask와 무관하게 completion을
  drain했다.
- 위반: polling spec은 callback이 completion wait를 실행한 thread에서 dispatch되며 일반
  `recv_part`가 completion을 drain하지 않는다고 정한다.
- 대안 A: `read_activated()`는 readiness만 기록하고 completion owner가 있는 wait 또는 async
  mailbox drain 지점에서만 pipe를 읽는다.
- 대안 B: 임의 API thread와 I/O thread callback을 public contract로 허용한다.
- 선택: A를 적용한다. `ZLINK_POLLCOMPLETION`을 요청한 wait와 명시적인 async mailbox drain
  scope만 handler 실행 권한을 가진다.

#### CR1-F04 — poller modify가 completion mode를 잘못 허용한다

- 근거: bare `ZLINK_POLLCOMPLETION`은 `zlink_poller_modify()` 검증을 통과하지만 add 경로의
  ownership acquire를 수행하지 않는다.
- 대안 A: spec대로 modify에서 해당 bit를 항상 거부한다.
- 대안 B: modify 전환에 acquire·release와 contract test를 추가하고 spec을 바꾼다.
- 선택: A를 적용한다. 승인된 계약에 mode 전환이 없으므로 새 상태 전이를 추가하지 않는다.

#### CR1-F05 — monitor HWM count를 byte로 잘못 옮겼다

- 근거: `4096 messages`였던 내부 monitor pipe 값을 타입만 `uint64_t`로 바꾸어
  `4096 bytes`가 되었다. 작은 event만 사용해도 보관 가능한 record 수가 크게 줄어든다.
- 대안 A: 이전 count에 minimum Core charge를 곱한 byte 값으로 옮긴다.
- 대안 B: 수동 override를 제거하고 Auto HWM을 사용한다.
- 선택: A를 적용한다. 기존 monitor burst capacity를 유지하고 Auto profile 변화와 분리한다.

#### CR1-F06 — 64-bit get의 buffer 크기 계약이 다르다

- 근거: 구현과 test는 `sizeof(uint64_t)`와 정확히 같은 buffer만 허용하지만 spec은 그보다
  큰 capacity도 허용하는 문장으로 작성되었다.
- 대안 A: exact-size 계약을 spec에 명시하고 큰 buffer 거부 test를 추가한다.
- 대안 B: 구현을 capacity 계약으로 바꾼다.
- 선택: A를 적용한다. Set과 get이 같은 exact-size 규칙으로 4-byte legacy 호출을 거부한다.

#### CR1-F07 — paired endpoint disconnect 회귀가 부족하다

- 근거: 기존 reconnect test는 application payload만 확인하고 두 lane 중 하나가 명시적
  disconnect 뒤 계속 연결되는지 검사하지 않았다.
- 대안 A: 기존 request/reply test에 monitor event 기반 pair disconnect 회귀를 추가한다.
- 대안 B: 별도 pair lifecycle fixture를 만든다.
- 선택: A로 명시적 disconnect 뒤 추가 connect event가 없음을 검증하고, raw ZMP fixture에서
  같은 pair identity와 다른 generation을 가진 두 lane이 Application pipe로 결합되지 않음을
  별도로 검증한다.

#### `Low` finding 처리

| ID | 내용 | 처리 |
| --- | --- | --- |
| CR1-F08 | 제거한 dispatch 시대 state와 no-op helper가 남아 있음 | Core 동작 gate를 바꾸지 않는 cleanup이다. bindings 완료 뒤 별도 정리 대상으로 둔다. |
| CR1-F09 | ZMP internals 문서가 paired transport의 강제 metadata 예외를 설명하지 않음 | 다음 candidate의 문서 동기화에 포함한다. |
| CR1-F10 | req/rep hot path의 lock·vector·linear scan | C-07 측정에서 regression이 없었다. CPU/message에서 2%를 넘을 때만 구조 변경을 검토한다. |
| CR1-F11 | lane 2 connect 실패 시 lane 1 unwind가 원자적이지 않음 | OOM·resolver failure fault injection을 추가하는 후속 lifecycle 작업으로 남긴다. |
| CR1-F12 | blocking send의 retry errno remap이 오류 문서에 없음 | 다음 candidate의 오류 문서 동기화에 포함한다. |
| CR1-F13 | `zlink_ctx_get_data()`가 현재 option 하나만 지원함 | 승인 범위에 새 public API 일반화를 추가하지 않는다. Exact 지원 목록을 contract에 명시한다. |

### 판정 (CR-07)

Round 1은 `NOT CLEAN`이다. 통합 결과는 `Critical` 2건, `High` 4건, `Medium` 6건이다.
CR1-03과 Claude Fable의 empty-pipe finding은 같은 원인으로 합쳤다. 수정 후 새 candidate
SHA에서 두 reviewer의 전체 review를 다시 받아야 한다.

## Round 2

Candidate `d314e96fd8`을 `/tmp/zlink-core-candidate-d314e96fd8`에 고정하고 비교 기준
`8bc2aa6786`, rubric v1로 전체 범위를 다시 검토했다.

| Reviewer | Model | Reasoning | 실행 시각 | Report | 판정 |
| --- | --- | --- | --- | --- | --- |
| Codex 5.6 High | `gpt-5.6-sol` | high | 2026-07-30 14:07 | `/tmp/zlink-review-results/codex-round2.md` | `NOT CLEAN` |
| Claude Fable | `claude-fable-5` | high | 2026-07-30 13:57 | `/tmp/zlink-review-results/fable-round2.md` | `NOT CLEAN` |

두 reviewer는 unbounded completion control queue를 `Critical`로 함께 보고했다. Codex는
incomplete multipart가 빈 pipe 예외를 반복해서 사용할 수 있는 문제를 `Critical`, inproc에서
`MaxMessageSize`를 독립적으로 강제하지 않는 문제를 `High`로 추가 확인했다. 통합한
`Medium` finding은 pair lifecycle wire matrix, completion poll의 전체 peer scan과 C benchmark의
4-byte HWM 호출이다.

대안 검토 결과는 다음과 같다.

- Request admission 때 유한한 completion slot을 예약하고 callback 실행 직전에 반환한다.
  Timeout과 disconnect는 새 memory를 확보하지 않고 기존 예약을 control record로 바꾼다.
- Multipart는 누적 payload와 accounted byte를 따로 관리한다. `MaxMessageSize`는 HWM과
  무관하게 payload에 먼저 적용하고, incomplete `MORE` frame은 일반 HWM을 넘지 못하게 한다.
- Completion readiness는 pair table 전체를 다시 훑지 않고 ready key를 한 번만 queue에 넣어
  실제 ready pair만 drain한다.
- Raw wire fixture는 pair id, generation, peer identity, duplicate lane과 reconnect 뒤 이전
  generation completion frame을 각각 검증한다.
- C benchmark는 HWM을 `uint64_t` byte로 전달하고 설정 실패를 즉시 중단한다.

수정 candidate `5329e79a2f`은 CPU concurrency를 2로 제한한 Core build와 82/82 test를
통과했다. Round 3에서 같은 SHA의 두 전체 review를 다시 수행한다.

## Round 3

Candidate `5329e79a2f`을 `/tmp/zlink-core-candidate-5329e79a2f`에 고정하고 비교 기준
`8bc2aa6786`, rubric v1로 전체 범위를 다시 검토했다.

| Reviewer | Model | Reasoning | 실행 시각 | Report | 판정 |
| --- | --- | --- | --- | --- | --- |
| Codex 5.6 High | `gpt-5.6-sol` | high | 2026-07-30 15:01 | `/tmp/zlink-review-results/codex-round3.md` | `NOT CLEAN` |
| Claude Fable | `claude-fable-5` | high | 2026-07-30 15:00 | `/tmp/zlink-review-results/fable-round3.md` | `NOT CLEAN` |

Codex는 `High` 4건과 `Medium` 1건을 보고했다. Completion reservation을 callback 종료 전에
반환하던 문제, `EMSGSIZE`를 `EAGAIN`으로 바꾸면서 pipe를 비활성화하던 문제, 빈 pipe의
유효한 oversize multipart를 거부하던 문제, connect-before-bind conflate 경로가 실제 reader의
`MaxMessageSize`를 적용하지 않던 문제와 이전 count 단위가 남은 한국어 guide다.

Claude Fable은 `Critical` 2건, `Medium` 3건과 `Low` 2건을 보고했다. Completion pipe write
실패 때 reply frame 소유권을 반환하지 않던 문제, byte admission 거부 뒤 pipe가 회복되지 않던
문제, 호출되지 않는 router requester stack, DEALER peer 하나가 끊겼을 때 모든 pending request를
실패시키던 문제와 두 C monitor header 사이의 drift gate 부재다. Pipe 비활성화 finding은 Codex
finding과 같은 원인으로 통합했다. `Low` 2건은 hiccup 뒤 작은 accounting 잔여값과 monitor ABI의
caller-size negotiation 부재다. 첫 항목은 bounded skew이고 두 번째는 Core 11 breaking release의
동일 release header/runtime 계약 안에서 허용하므로 이번 gate의 수정 대상에 포함하지 않았다.

수정 방향은 다음과 같이 결정했다.

- Completion reservation은 각 callback이 반환한 직후 한 건씩 반환한다. Blocking callback을
  사용해 callback 실행 중에도 총 미완료 request가 65,536건을 넘지 않는 회귀를 추가했다.
- `MaxMessageSize` 위반은 pipe를 비활성화하지 않고 `EMSGSIZE`를 유지한다. Oversize 거부 뒤
  정상 message를 보낼 수 있는 회귀를 추가했다.
- 빈 pipe에서 시작한 multipart는 transaction 수명 동안 empty-pipe exception을 한 번 유지하고,
  누적 payload에는 `MaxMessageSize`를 계속 적용한다.
- Pending inproc connection도 conflate 여부와 무관하게 실제 reader의 최대 message 크기를 각
  방향 pipe에 적용한다.
- Completion send helper가 성공과 실패 모두에서 남은 frame 소유권을 소비하도록 통일했다.
- 호출되지 않는 router requester state와 lifecycle bridge를 제거했다.
- DEALER request admission에 load balancer가 선택한 transport pair id와 generation을 기록하고,
  pair가 끊겼을 때 그 pair의 request만 `ZLINK_REQUEST_NOT_CONNECTED`로 끝낸다.
- Core와 C binding monitor ABI mirror가 달라지면 CTest가 실패하는 build-time 검사와 한국어 guide
  수정을 추가했다.
- Binding 사전 적용에서 찾은 `AUTO_HWM_MSG_UNIT_BYTES=UINT64_MAX` 거부도 함께 수정했다. Auto
  계산은 overflow 때 `UINT64_MAX`로 saturate하고 context·socket set/get과 snapshot 회귀로
  경계값을 고정했다.

수정 candidate `d9df28edee`는 `taskset -c 0,1`, `nice -n 10`, build `-j 2` 조건에서
최종 build를 통과했다. 같은 제한으로 targeted CTest 6/6과 전체 CTest 83/83이 통과했다.
DEALER selective disconnect case는 1/1이 통과했다. Completion backpressure case의 Valgrind는
definite·indirect leak 0 byte, error 0건이다. Round 4에서 같은 SHA의 두 전체 review를 다시
수행한다.

## Round 4

Candidate `d9df28edee`를 `/tmp/zlink-core-candidate-d9df28edee`에 고정했다. 비교 기준은
`8bc2aa6786`이고 두 reviewer에게 rubric v1과 전체 `core`, `bindings/c` 범위를 동일하게
제공했다.

| Reviewer | Model | Reasoning | 실행 시각 | Report | 판정 |
| --- | --- | --- | --- | --- | --- |
| Codex 5.6 High | `gpt-5.6-sol` | high | 2026-07-30 | `/tmp/zlink-review-results/codex-round4.md` | `NOT CLEAN` |
| Claude Fable | `claude-fable-5` | high | 2026-07-30 | `/tmp/zlink-review-results/fable-round4.md` | `NOT CLEAN` |

Codex는 `Critical` 2건, `Medium` 2건과 `Low` 1건을 보고했다. PUB/XPUB의 incomplete
multipart가 finite `MaxMessageSize`를 넘을 수 있는 문제, ROUTER completion이 sequence만으로
다른 peer의 pending request와 결합될 수 있는 문제, C multi perf의 4-byte HWM, C umbrella
header version drift와 제거되지 않은 dispatch lifecycle state다.

Fable은 C binding header tree에 제거된 Core 10 service ABI가 남은 문제를 `High`, WS/WSS
paired transport가 lane마다 `CONNECTION_READY`를 내는 문제를 `Medium`으로 보고했다. 조기
credit 반환의 spec 누락, dead dispatch state, options pair lifecycle, ZMP/WS ready state 중복,
ROUTER cold-path peer scan과 일부 failure unwind는 `Low`다.

수정 방향은 다음과 같이 결정했다.

- PUB/XPUB admission은 typed result로 size 위반과 HWM을 구분하고 incomplete multipart의 매
  frame에서 누적 payload를 검사한다. Oversize 거부 뒤 정상 message가 통과하는 회귀를 추가했다.
- ROUTER completion correlation은 peer identity, transport pair id와 generation을 모두
  일치시킨다. Sequence-only fallback을 제거하고 다른 peer가 같은 sequence를 보내도 pending
  request를 소비하지 않는 회귀를 추가했다.
- C와 C++ perf의 HWM parser·storage·option 전달을 끝까지 `uint64_t`로 통일하고 경계값 test를
  추가했다.
- C binding public header tree를 Core 11 raw-only header tree와 byte 단위로 맞추고 service
  header·test·sample을 제거했다. 일부 ABI만 비교하던 gate를 전체 header tree 비교로 넓혔다.
- WS/WSS engine은 negotiated pair identity를 보관하고 두 lane이 모두 준비된 뒤에만 연결당 한
  번 `CONNECTION_READY`를 보고한다. WS와 WSS 회귀를 추가했다.
- 사용되지 않는 dispatch lifecycle state와 설치 helper를 제거했다. Empty pipe에서 input을
  모두 배수하면 LWM 이전에도 credit을 반환할 수 있다는 동작을 정식 socket spec에 명시했다.
- 나머지 `Low` finding은 실제 측정에서 병목이 확인될 때 registry나 ready state를 통합하고,
  fault injection 범위를 확장할 때 lifecycle unwind를 함께 다루는 후속 대상으로 둔다.

### 이후 reviewer 정책

2026-07-30 사용자 결정에 따라 Round 4 이후의 새 review는 Codex review만 수행한다. Round 4까지
완료된 Fable finding은 폐기하지 않고 위 수정에 반영하지만, 새 candidate 재검토와 bindings
review에는 Fable을 실행하지 않는다. 새 candidate에서 Codex 전체 review의 `Medium` 이상
finding이 0건일 때 `CLEAN`으로 판정한다.

## Round 5

Candidate `3c62117865`를 `/tmp/zlink-core-candidate-3c62117865`에 고정하고 비교 기준
`8bc2aa6786`, rubric v1로 Codex 전체 review를 수행했다. 사용자 정책에 따라 Fable review는
추가하지 않았다.

| Reviewer | Model | Reasoning | 실행 시각 | Report | 판정 |
| --- | --- | --- | --- | --- | --- |
| Codex 5.6 High | `gpt-5.6-sol` | high | 2026-07-30 | `/tmp/zlink-review-results/codex-round5.md` | `NOT CLEAN` |

Codex는 `Critical` 2건, `High` 1건과 `Medium` 2건을 보고했다. Network pipe의 unfinished
multipart 전체 크기에 유한한 상한이 없던 문제, 수신 request의 reply-target map이 제한 없이
증가하던 문제, paired lane이 handshake에서 확인한 peer identity 대신 mutable routing state를
비교하던 문제, receive byte monitoring의 data race와 input이 빌 때마다 LWM 전에 credit
command를 보낼 수 있던 문제다.

POSD의 두 번 설계 원칙에 따라 다음 대안을 비교했다.

- Network message 상한은 peer limit를 READY metadata로 교환하는 방법과 empty-pipe exception을
  전부 제거하는 방법을 비교했다. 전자를 선택해 local reader limit와 peer limit를 방향별 pipe에
  한 번 설정했다. `MaxMessageSize`가 무제한인 경우 complete message 한 건의 liveness 예외는
  유지하지만 unfinished multipart에는 일반 byte HWM을 적용한다.
- Reply target은 수신 전에 bounded slot을 예약하는 방법과 public opaque reply handle로
  correlation을 옮기는 방법을 비교했다. Public contract를 바꾸지 않고 lifecycle owner가
  admission과 release를 함께 관리하는 65,536 slot reservation을 선택했다.
- Pair identity는 handshake identity 전용 immutable field와 기존 routing ID를 양 endpoint에
  복사하는 방법을 비교했다. Routing handover와 transport 검증을 분리하는 immutable field를
  선택했다. Inproc에는 실제 peer socket instance를 identity로 사용하고, pending connect는 peer가
  정해질 때까지 pair validation을 보류한다.
- Monitoring counter는 모든 write를 atomic으로 바꾸는 방법과 진단 getter에서 writer lock을
  사용하는 방법을 비교했다. Message write hot path를 바꾸지 않는 lock snapshot을 선택했다.
- Credit은 LWM-only 방식과 HWM-blocked recovery를 비교했다. LWM-only는 낮은 backlog가 먼저
  배수된 뒤 sender가 HWM에 도달하면 회복 command가 없어 실제 stall을 만들었다. Sender가 HWM
  판정에 실패할 때만 peer의 monotonic read snapshot을 확인하고, 이후 input이 모두 배수되면
  한 번 조기 credit을 보내는 방식을 선택했다. 정상 ping-pong에서는 LWM batching을 유지한다.

회귀 검증은 network에서 6 byte `MORE` frame 두 개가 `MaxMessageSize=10`을 넘는 경우, 서로 다른
peer identity의 두 lane이 payload 전송 전에 종료되는 경우, ROUTER reply target 65,536개 상한과
reply 뒤 slot 재사용을 포함한다. Targeted CTest는 9/9, 전체 build는 `taskset -c 0-9`,
`nice -n 10`, `-j10`으로 통과했다. 전체 CTest는 non-serial 20/20(`-j10`)과 serial
63/63(`-j1`)으로 나누어 83/83이 통과했다. 실행 log는 각각
`/tmp/zlink-r5-target-ctest-final.log`, `/tmp/zlink-r5-full-build.log`,
`/tmp/zlink-r5-ctest-nonserial.log`, `/tmp/zlink-r5-ctest-serial.log`에 있다.

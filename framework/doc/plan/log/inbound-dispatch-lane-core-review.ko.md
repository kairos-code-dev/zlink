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
| Claude Fable | 보고서 기록값 | 보고서 기록값 | 2026-07-30 11:58 시작 | 실행 중 |

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

### 판정 (CR-07)

Round 1은 `NOT CLEAN`이다. Codex 보고서만으로도 `Critical` 2건, `High` 1건, `Medium` 2건이
남아 있다. Claude Fable review가 끝나면 finding을 합치고, 수정 후 새 candidate SHA로 두
reviewer의 전체 review를 다시 받아야 한다.

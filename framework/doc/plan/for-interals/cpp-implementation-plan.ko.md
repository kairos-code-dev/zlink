# C++ framework 구현 계획

[계획 문서 묶음](README.ko.md) · [구현 갭 목록](framework-internals-implementation-gaps.ko.md)

판정과 근거는 [구현 갭 목록](framework-internals-implementation-gaps.ko.md)이 소유한다.
이 문서는 **무엇을 어떤 순서로 고칠지**만 다룬다.

> **여기 적힌 `file:line`은 길잡이다.** 판정의 근거가 아니며, 갭 목록과 어긋나면 **갭
> 목록이 맞다.** 새 사실을 발견하면 갭 목록을 먼저 고친다.

## 이 언어의 특징

**정식 ClientServer 경로는 다른 세 언어와 같다** — framework가 후보 server를 고르고 그
server 전용 연결로 보낸다(`runtime/client_server/client_server_location_runtime.cpp:699,1316-1328`,
`runtime/client_server/raw_client_server_owner.cpp:668`). A1은 framework 안에서 닫힌다.

C++에만 있는 것은 **fallback 경로**다. ClientServer transport가 등록되지 않은 채널은 하나의
DEALER에 endpoint를 모두 연결하고 Core가 고른다
(`runtime/channels/channel_outbound_exchange.cpp:931` 분기, `:1015` fallback 생성). 이
경로에 죽은 선택 코드가 남아 있어 정리 대상이다.

[Core 수정](dealer-weighted-selection-order.ko.md)은 **이 작업의 선행 과제가 아니다.**
Core 계약을 넓히는 독립 제안이다.

실행 자원은 OS worker pool이므로 두 실행 영역 분리와 owner 점유 상한을 물리적으로 구현할
수 있다. Node처럼 event loop 제약을 받지 않는다.

## 순서

```
0. 선행 판정 대기        상한 값 · exact interface
   │
1. W7 오류 kind          작아서 먼저. 이후 묶음이 이 kind를 쓴다
   │
2. W1 scheduler          가장 큼. B2가 여기 포함된다
   │
3. W6 수신 공정성        W1의 batch 구조를 재사용한다
   │
4. W2-RM selector        선행 과제 없음
   4b. W2-CS selector      per-server 구조 유지. 절차만 교체
   │
5. W5 관찰자             exact interface 확정 후
   │
6. W4 유휴 정리          신규 기능. 범위 결정에 따라 미뤄질 수 있다
   │
7. W3 relay 통지         wire command 확정 후
   │
8. 독립 항목             B3 · C부
```

## W1 — scheduler (가장 큼)

한 단위로 묶는 항목: **A4 · A6 · A8 · A10 · B2 · D2**

`serial_execution_queue`를 중심으로 제출부터 회계 반납까지 전 구간을 다시 짠다. 따로 하면
같은 자리를 네 번 고치게 된다.

| 단계 | 지금 | 목표 |
|---|---|---|
| 한도 | 건수만 (`runtime/execution/serial_execution_queue.cpp:296-317`) | 건수·byte 두 축을 **하나의 작업으로** 예약 |
| lane | 단일 queue + 앞쪽 삽입 barrier (`:360-374`) | application·lifecycle 두 FIFO lane, 각자 한도 |
| 선택 | FIFO | lane 선택 + lifecycle 연속 실행 상한 + 양보 부채 |
| 점유 | 작업 하나마다 재예약 (`:452,510,514`) | ready owner 집합 + claim 시작 시각 + 경과 시간 확인 |
| 포화 | `Rejected` 반환 (`runtime/spots/spot_runtime.cpp:924-935,2426-2429`) | 계열×위치 표에 따른 kind |
| 반납 | — | handler 종료 후 두 축 동시 반납 |

**B2를 이 안에서 함께 고친다.** `:129-146`의 `resume_scheduler()`가 continuation을 재등록하지
못하면 호출 스레드에서 그대로 실행한다. 두 축 예약이 들어오면 실패 경로가 늘어나므로 이
inline 실행을 **terminal failure로 완료**하도록 바꾸는 것이 함께 필요하다.

**D2는 별도 파일이지만 같은 회계다.** `runtime/dispatch/inbound_dispatch_budget.hpp:32,42,53,62,69`가
수신·handler 시작·완료·snapshot에 같은 mutex를 쓴다. HWM 판정용 `pendingBytes`만 process
단위 atomic으로 남기고 관측 누계는 shard로 분리한다.

**참조 구조가 이미 있다.** `runtime/mesh/service_mailbox.cpp:52-53`이 message·byte 두 축을
모두 강제한다. 실행 queue를 같은 형태로 맞춘다.

**주의 — `service_mailbox`의 단일 mutex 구조를 그대로 확장하면 안 된다.** 모든 owner가 같은
lock에서 직렬화된다(`runtime/mesh/service_mailbox.cpp:43`). 두 축 예약은 각 owner queue의
기존 lock 안에서 하나의 상태로 처리하고, 전역 scheduler에는 비어 있다가 ready로 바뀌는
전이만 알린다.

## W6 — 수신 공정성

항목: **A9**

C++의 fanout·ClientServer 수신 경로는 현재 message·byte·time batch와 회전 cursor를
사용한다. 이 변경은 공통 internals의 코드 조건을 반영한 것이며, 실제 연결이 계속 유입될
때 뒤쪽 connection과 control 신호가 진행하는지는 process matrix에서 별도로 확인한다.

| 경로 | 위치 |
|---|---|
| fanout | `runtime/fanout/fanout_location_runtime.cpp:298-340`, 내부 순회 `runtime/fanout/raw_fanout_owner.cpp:266-333` |
| ClientServer | `runtime/client_server/client_server_location_runtime.cpp:947-948,966-967` `for(;;) pump_one` |

각 drain에 **건수·byte·경과 시간** 상한을 적용하고 다음 실행은 회전 cursor에서 시작한다.
W1에서 만든 batch 구조를 재사용한다.

**범위가 넓어졌다.** 조항이 이제 RouteMesh·service·STREAM까지 포함하므로 그 경로들도
감사 대상이다.

## W2 — selector

항목: **A1 · D1**

**per-server 연결 구조는 유지한다.** 승인 상태를 하위 계층에 투영할 경로가 없어 합칠 수
없다는 것이 A1b 판정이다. 선택 **절차만** 고친다.

| 경로 | 지금 | 할 일 |
|---|---|---|
| RouteMesh 채널 | 후보 변경 시 smooth weighted schedule을 계산하고, fallback에서도 누적값과 Node RID tiebreak를 사용한다 (`runtime/mesh/service_topology_registry.cpp:426-650`) | **구현 완료.** schedule을 만들 수 없는 경계는 누적값 fallback으로 처리한다 |
| ClientServer | 후보에 connection key와 별도의 Server RID tiebreak key를 보관하고, 후보 변경 시 schedule을 계산한다 (`runtime/client_server/weighted_selector.hpp`, `runtime/client_server/client_server_location_runtime.cpp:1316-1327`) | **구현 완료.** connection generation이나 endpoint가 tiebreak를 대신하지 않는다 |
| **fallback** DEALER fan | ~~winner를 계산해 목록을 회전시키지만 받는 쪽이 집합에 넣어 순서를 지운다~~ | **✔ 완료** — 선택·회전과 죽은 `_auto_connections`를 삭제하고 호출처를 `list_manual_connections()`로 돌렸다 |

**삭제 완료.** `_auto_connections`는 죽은 경로로 확인됐다 — 쓰기가 테스트에만 있고 나머지
API는 호출처가 0이었다. Production bundle은 `runtime/channels/channel_bundle_factory.cpp:49-55`가
manual만 채운다.

**D1 — 선택 비용.** 호출마다 후보 map을 새로 만들고 전체 credit을
갱신하는 비용(`runtime/client_server/weighted_selector.hpp:29`)을 후보 변경 시점으로 옮긴다. 주기를 미리
계산할 때 **시작 상태 복귀를 조건으로 삼으면 안 된다** — 도입부를 지나면 시작 상태는 다시
나오지 않는다. 같은 상태가 다시 나타나는 지점을 찾는다.

## W5 — 관찰자

항목: **A11 · A12**

`runtime/client_server/client_server_location_runtime.cpp:217-268`이 가득 차면 오래된 것을
버린다. source별 합치기로 바꾸고 terminal 보관 상한과 관찰자별 유실 누계를 넣는다.

**관찰자마다 OS thread를 만드는 구조도 함께 본다**(`:230`). 관찰자 수에 비례해 thread가
늘어난다. 공유 dispatcher와 수요 기반 queue로 바꾸는 것을 함께 검토한다.

전달 형태가 `{status, 유실 누계}` 쌍으로 바뀌므로 **exact interface 확정이 선행**이다.

## W4 — 유휴 정리 (신규 기능)

항목: **A3**

`include/zlink/framework/contracts/spots/spot.hpp:101-106`에 `idle_evicted`를 추가하는 것은
시작일 뿐이다. Location Store CAS·admission seal·timer 구조가 함께 필요하다.

**timer는 Spot당 예약 항목을 최대 하나로 유지한다.** 활동할 때마다 새 항목을 넣고 낡은
것을 만료까지 두면 쌓이는 항목이 `활동률 × 유휴 기준 시간`에 비례한다.

## W3 — relay 통지

항목: **A2 · D5**

command 50 `messageFollow`의 C++ local path는 구현했다. relay 성공 뒤 source runtime에
통지를 보내고 route fence가 맞는 cache만 무효화하는 codec·dispatch·source handler가 현재
worktree에 있다. 남은 조건은 다른 언어와 섞은 process에서 command body, 인증, fence와 중복
억제가 같은 결과를 내는지 확인하는 것이다.

## 독립 항목

**B3 — `preparing → serving` 순서.** bind와 주소 확정 뒤에도 descriptor를 `preparing`으로
유지하고 `_port`, completion control, monitor와 router 준비를 끝낸 뒤 `serving`을 게시한다
(`runtime/mesh/raw_mesh_node_owner.cpp`). 관련 owner-layer 구현과 vertical unit은 통과했다.

**C부 구조 부채.** application이 관찰할 수 없으므로 우선순위가 가장 낮다.

- operation ID 3종과 exactly-once 3종을 각각 하나로 통일
- `<zlink.hpp>`를 직접 include하는 15개 파일 정리
- Spot kind를 bool 플래그가 아니라 타입으로

## 검증

W1·W6은 [2. Spot·Actor 실행 직렬화](../../framework/common/internals/02-serialization.ko.md)과
[7. 수신과 dispatch 루프](../../framework/common/internals/07-dispatch-loop.ko.md)의
"확인할 결과"를 그대로 쓴다. W2는 같은 weight와 다른 weight 두 경우에서 실제 server
process가 받은 순서를 확인하는 process test가 필요하다.

## 2026-08-04 구현 및 검증 기록

현재 C++ 구현에 반영할 수 있는 계획 항목은 owner layer와 public contract를 기준으로
구현했다. W1은 `serial_execution_queue`에서 application·lifecycle 두 FIFO lane을 건수와
byte로 함께 예약하고, lifecycle burst/debt와 after-active phase를 사용한다. queue가
포화된 뒤 await continuation을 호출자 stack에서 실행하지 않고 별도 executor turn에서
`capacity_exceeded` terminal 결과로 끝내는 회귀 test도 추가했다.

W2·D1은 RouteMesh 후보의 Node RID와 ClientServer 후보의 Server RID를 tiebreak 기준으로
사용하고, 후보 변경 시 선택 상태를 다시 계산한다. W4는 Instance Spot idle eviction과
admission seal을, W5는 공유 observer dispatcher·bounded/coalesced delivery·terminal
보관 상한을, W6은 RouteMesh·ClientServer·service·fanout 수신 경로의 회전 cursor와
message·byte·time batch를 사용한다. D2의 HWM pending 회계와 B3의 주소 게시 후
`serving` 공개 순서도 현재 경로에서 확인했다. operation ID와 exactly-once table을 공통
runtime helper로 모으고, Spot kind를 bool이 아닌 enum으로 바꾸며, C++ 내부 test의
umbrella `<zlink.hpp>` 의존도 제거했다.

구현 비교에는 공통 internals의 [직렬 실행](../../framework/common/internals/02-serialization.ko.md),
[대상 선택과 위치 캐시](../../framework/common/internals/06-routing-and-cache.ko.md),
[수신과 dispatch 루프](../../framework/common/internals/07-dispatch-loop.ko.md),
[객체 lifecycle](../../framework/common/internals/08-object-lifecycle.ko.md),
[Session과 Actor 연결](../../framework/common/internals/09-session-binding.ko.md),
[Liveness와 상태 공개](../../framework/common/internals/10-liveness-and-state.ko.md),
[Payload 소유권과 복사](../../framework/common/internals/11-message-ownership.ko.md)와
`service-wire-protocol.ko.md`를 사용했다. STREAM은 listener-wide readiness scheduler가
여러 session의 receive cursor를 회전시키고, session dispatch는 각 session의 serial queue에서
수행한다. 따라서 공통 internals가 요구하는 connection-level 회전 구조는 현재 코드에
반영되어 있다. 다만 STREAM을 포함한 전체 topology 행렬의 공정성은 별도 process 검증
없이는 완료 판정하지 않는다.

검증은 다음과 같다.

- `cmake --build framework/languages/cpp/build -j2`: 100% target build 통과
- `framework-unit`: 31/31 PASS
- `framework-contract`: 8/8 PASS
- `test_cpp_framework_install_consumer`, `test_cpp_framework_tooling_contract`: PASS
- 전체 CTest: 수정 후 fresh aggregate에서 56/56 PASS, `100% tests passed, 0 tests failed out of 56`을
  확인했다. 여기에는 unit·contract, installed package consumer, connector·HTTP와 6개
  framework sample이 포함된다.
- ASan targeted gate: `actor_gateway`, `execution`, `channel_messaging`, `stream_framework`
  4/4 PASS. LeakSanitizer 오류 없이 종료했다.
- framework sample smoke: 전체 aggregate 6/6 PASS를 확인했다. 추가 반복은 TicTacToe 10회,
  GameQuest 12회, SupportChat 15회 연속 PASS였다.
- `RegistryMessaging/run_e2e.sh RM-C7`: 두 provider process가 `api-a=180`, `api-b=60`을
  기록하고 PASS

W3(A2·D5)는 C++ local path를 구현했지만 command 50의 mixed-process 상호운용 검증은 남아
있다. D6의 batch·owner 점유 상한 값은
현재 C++ 기본값과 unit evidence가 있지만 공통 contract의 측정·허용 범위가 확정된 것은
아니다. 이 두 조건과 STREAM 전체 process matrix는 남은 설계·검증 항목으로 분리한다.

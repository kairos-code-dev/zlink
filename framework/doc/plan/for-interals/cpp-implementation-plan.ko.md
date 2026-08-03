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

C++은 확정 갭이 둘이다. 두 경로 모두 데이터가 없어질 때까지 한 대상을 완전히 drain한다.

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
| RouteMesh 채널 | cursor를 weight 합으로 나눈 연속 구간 (`runtime/mesh/service_topology_registry.cpp:422-465`) | 누적값 3단계로 교체. 후보 map key가 NodeRid라 tiebreak는 이미 맞다 |
| ClientServer | 누적값은 맞지만 tiebreak 키가 **연결 map key** (`runtime/client_server/weighted_selector.hpp:24-60`, 후보 구성 `runtime/client_server/client_server_location_runtime.cpp:1316-1327`) | 후보에 Server RID를 별도 field로 실어 보내고 tiebreak가 그 값만 비교하게 한다 |
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

`runtime/actors/actor_client.cpp:591-609,746-756`이 relay **실패** 때만 캐시를 지운다. 성공
relay를 송신 측에 알리는 경로가 없다. wire command가 확정되기 전에는 시작할 수 없다.

## 독립 항목

**B3 — `preparing → serving` 순서.** `runtime/mesh/raw_mesh_node_owner.cpp:293-305`가
descriptor를 `serving`으로 게시한 뒤 `_port`와 completion control을 만든다. bind와 주소
확정 뒤에도 `preparing`을 유지하고, 준비가 끝난 뒤 별도 단계에서 `serving`을 게시한다.
다른 묶음과 겹치지 않으므로 언제든 진행할 수 있다.

**C부 구조 부채.** application이 관찰할 수 없으므로 우선순위가 가장 낮다.

- operation ID 3종과 exactly-once 3종을 각각 하나로 통일
- `<zlink.hpp>`를 직접 include하는 15개 파일 정리
- Spot kind를 bool 플래그가 아니라 타입으로

## 검증

W1·W6은 [2. 직렬 실행](../../framework/common/internals/02-serialization.ko.md)과
[7. 수신과 dispatch 루프](../../framework/common/internals/07-dispatch-loop.ko.md)의
"확인할 결과"를 그대로 쓴다. W2는 같은 weight와 다른 weight 두 경우에서 실제 server
process가 받은 순서를 확인하는 process test가 필요하다.

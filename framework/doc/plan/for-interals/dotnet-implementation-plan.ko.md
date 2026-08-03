# .NET framework 구현 계획

[계획 문서 묶음](README.ko.md) · [구현 갭 목록](framework-internals-implementation-gaps.ko.md)

판정과 근거는 [구현 갭 목록](framework-internals-implementation-gaps.ko.md)이 소유한다.
이 문서는 **무엇을 어떤 순서로 고칠지**만 다룬다.

> **여기 적힌 `file:line`은 길잡이다.** 판정의 근거가 아니며, 갭 목록과 어긋나면 **갭
> 목록이 맞다.** 새 사실을 발견하면 갭 목록을 먼저 고친다.

## 이 언어의 특징

두 채널 종류 모두 framework 계층에서 대상을 고르므로 **Core 수정에 걸리지 않는다.**
C++와 달리 W2를 독립적으로 진행할 수 있다.

실행 자원은 thread pool 위의 직렬 drain이다. 두 실행 영역 분리와 owner 점유 상한을 물리적
자원으로 구현할 수 있다.

가장 무거운 것은 **관찰자 stream 재작성**이다. 두 자리가 합치기 대신 버리고 있어 구조를
바꿔야 한다.

## 순서

```
0. 선행 판정 대기        상한 값 · exact interface
   │
1. B4 무음 드롭          가장 작고 위험도 높음. 지금도 응답이 사라진다
   │
2. W1 scheduler          가장 큼
   │
3. W5 관찰자             exact interface 확정 후. 구조 변경 큼
   │
4. W2 selector           선행 과제 없음
   │
5. W6 수신 공정성        채널 loop은 충족. 나머지 topology 감사 필요
   │
6. W4 유휴 정리          신규 기능
   │
7. W3 relay 통지         wire command 확정 후
   │
8. C부 구조 부채
```

## B4 — completion table 무음 드롭 (먼저)

`Runtime/Backend/DotNet/ZLinkMeshCompletionTable.cs:58-73`이 4096개를 넘으면 parts를
dispose하고 그냥 반환한다. **기다리던 caller는 timeout으로만 알게 된다.**

**"그 operation을 즉시 완료한다"는 실행할 수 없다.** overflow는 `_pending`에 handler가
아직 없어서 `_early`에 넣으려는 시점에 일어난다. 완료해 줄 caller callback이 그 자리에
없다.

두 방향 중 하나를 고른다.

| 방향 | 내용 |
|---|---|
| tombstone | 버린 operation ID와 `CapacityExceeded`를 한도 있는 표에 남기고, 뒤늦게 `Register`하는 쪽이 그것을 소비해 실패로 끝낸다 |
| 수명 재설계 | 등록이 수신보다 항상 먼저 일어나도록 completion lifecycle을 바꾼다 |

어느 쪽이든 early completion과 tombstone의 **합산 건수·byte 한도**, parts 해제 소유권,
register·complete 경쟁을 같은 회귀 test로 고정한다.

## W1 — scheduler (가장 큼)

한 단위로 묶는 항목: **A4 · A6 · A8 · A10 · D2 · D3**

`ZLinkSerialExecutionQueue`를 중심으로 제출부터 회계 반납까지 다시 짠다.

| 단계 | 지금 | 목표 |
|---|---|---|
| 한도 | 건수만 (기본값 `:5`, 검사 `Runtime/Execution/ZLinkSerialExecutionQueue.cs:565-574`) | 건수·byte 두 축을 하나의 예약으로 |
| lane | 단일 queue. `reservedPrioritySlots`가 있으나 기본값 `0`이고 **0이 아닌 값을 넘기는 호출자가 없다** (`:42,50-52`) | application·lifecycle 두 FIFO lane |
| 선택 | FIFO (`:666-689`). infrastructure 우선 탐색은 relocation seal 중에만 (`:693-714`) | 상시 lane 선택 + 연속 실행 상한 + 양보 부채 |
| 점유 | `TryTakeNext`가 빌 때까지 drain (`:605-663`) | ready owner + claim 시작 시각 + 경과 시간 확인 |
| 포화 | `InvalidOperationException("…closed or full")` (`:110-118`), Spot 호출 `Runtime/Spots/ZLinkSpotSerialExecutor.cs:858-891` | 계열×위치 표에 따른 public kind. closed와 full을 구분 |
| 반납 | — | handler 종료 후 두 축 동시 반납 |

**예약 슬롯과 우선 실행은 다른 것이다.** `reservedPrioritySlots`는 자리 확보일 뿐이고
실행 순서를 바꾸지 않는다. A10은 *실행 순서*를 요구하므로 이 매개변수를 우선 실행으로
바꾸거나 쓰지 않으면 제거한다.

**D2 — 수신 회계 lock.** `Runtime/Dispatch/ZLinkInboundDispatchBudget.cs:64,93,99`가 수신·
handler 시작·완료·snapshot에 같은 `_gate`를 쓴다. HWM 판정용 `pendingBytes`만 process 단위
atomic으로 두고 관측 누계는 cache-line 분리 shard로 나눈다. `HandlerStarted`가 HWM 판정에
필요 없다면 metric counter로 분리해 hot path의 세 번째 동기화를 없앤다.

> **worker별 permit 덩어리 예약은 쓰지 않는다.** 쓰지 않은 local 잔량을 pending으로 세면
> HWM보다 일찍 멈추고, 세지 않으면 초과량이 커진다.

**D3 — HWM 초과량.** 수신 loop이 용량을 확인한 뒤 message 전체를 받고 **그 다음에야** 크기를
잰다(`Runtime/Channels/ZLinkChannelReceiveLoop.cs:38,41,56`). 현재 binding 경계에서는 선예약이
불가능하므로 두 방향 중 하나를 고른다 — binding에 길이 peek을 추가하거나, 초과량 공식을 spec에
명시하고 동시 수신 수를 제한한다. **W1 안에서 결정해야 회계 구조가 한 번에 정해진다.**

**D4 — 복사.** `Runtime/Messaging/ZLinkMessageRuntime.cs:82`가 envelope에서 payload를
복사하고, `Runtime/Execution/ZLinkSerialExecutionQueue.cs:257`이 queue 수락 시 이동 기록용
배열을 만든다. 이동 기록은 **봉인 이후에만** 만든다.

## W5 — 관찰자 (구조 변경 큼)

항목: **A11 · A12**

두 자리가 합치지 않고 버린다.

| 위치 | 지금 | 문제 |
|---|---|---|
| `Runtime/Locations/ZLinkFanoutRuntimeService.cs:302-312` | `DropOldest` | terminal status가 먼저 사라진다 |
| `Runtime/Channels/ZLinkClientServerRuntimeService.cs:136-151` | `DropWrite` | 최신 `Sequence`가 전달되지 않는다 |

`BoundedChannel`의 full mode로는 조항을 만족할 수 없다. **source별 최신 slot + terminal
보관 + 관찰자별 유실 누계** 구조를 새로 만들어야 한다.

전달 형태가 `{status, 유실 누계}` 쌍으로 바뀌므로 **exact interface 확정이 선행**이다.
`spec/server/languages/dotnet/interfaces/10-topology-monitoring.ko.md`에 유실 field가 없다.

관찰자마다 status를 복제하지 말고 불변 snapshot 하나를 공유하고 queue에는 참조와 순번만 넣는다.

## W2 — selector

항목: **A1 · D1**

**per-server DEALER 구조는 유지한다**(A1b 판정 완료). 선택 절차만 바꾼다.

| 경로 | 지금 | 할 일 |
|---|---|---|
| RouteMesh 채널 | 서로소 step ring (`Runtime/Service/ZLinkManagedMeshNode.cs:6688-6701`, `Runtime/Channels/ZLinkWeightedSelector.cs:16`). 게다가 **local target을 먼저 넣고 peer만 정렬**해 전체가 NodeRid 순서가 아니다 (`:6739-6759`) | 누적값 3단계로 교체 + 전체 후보를 NodeRid로 정렬 |
| ClientServer | 누적값 3단계는 맞다 (`Runtime/Channels/ZLinkClientServerClientRuntime.cs:430-451`). 그러나 `OrderByDescending(SelectionCurrent).First()`라 **동점이면 열거 순서가 결정한다** | Server RID 오름차순을 두 번째 키로 추가 |

.NET은 갭이 둘(절차·정렬)인 RouteMesh가 ClientServer보다 무겁다.

## W6 — 수신 공정성

항목: **A9**

채널 loop은 subscriber별로 한 record씩 처리해 **충족**이다
(`Runtime/Channels/ZLinkChannelReceiveLoop.cs:227-251`).

다만 조항 범위가 RouteMesh·ClientServer·service·STREAM까지 넓어졌으므로 채널 loop 판정으로
전체를 증명할 수 없다. **부분 확인 상태이며 나머지 경로 감사가 필요하다.**

## W4 — 유휴 정리 (신규 기능)

항목: **A3**

`Contracts/Spots/ZLinkSpot.cs:92-97`에 `IdleEvicted`를 추가하는 것은 시작일 뿐이다.
Location Store CAS·admission seal·timer 구조가 함께 필요하다.

## W3 — relay 통지

항목: **A2 · D5**

`Runtime/Spots/ZLinkActorMessageFollower.cs:647-705`가 follow 제출 성공 후 회계만 정리하고
반환한다. client 캐시는 `TargetNotFound`·stale 오류에만 반응한다
(`Runtime/Actors/ZLinkActorClient.cs:77,137`). wire command 확정 전에는 시작할 수 없다.

## C부 구조 부채

application이 관찰할 수 없으므로 우선순위가 가장 낮다.

- relocation 드라이버 3종을 단일 상태기계로
- `Runtime/Service/ZLinkManagedMeshNode.cs`(8,271줄) 분해 — 줄 수가 아니라 책임별 후보로
- `Runtime/Backend/Contracts`의 interface 26개 중 구현이 하나뿐인 것을 이름으로 나열해 정리
- `ZLinkLegacyMonitoringModels` 정리

## 검증

W1은 [2. 직렬 실행](../../framework/common/internals/02-serialization.ko.md), W5는
[10. Liveness와 상태 공개](../../framework/common/internals/10-liveness-and-state.ko.md)의
"확인할 결과"를 쓴다. W5는 **느린 관찰자를 인위적으로 만들어 두고** message 처리 속도가
유지되는지, terminal이 보존되는지, 유실 수가 관찰자마다 따로 세어지는지를 함께 본다.

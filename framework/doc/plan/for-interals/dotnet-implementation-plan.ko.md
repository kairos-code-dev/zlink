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

관찰자 stream은 source별 최신 상태와 terminal 보관을 분리하는 구조로 이미 재작성되었다.
현재 가장 큰 남은 조건은 command 50 relay 통지의 mixed-process 증거와 모든 topology의
수신 공정성 process matrix이다.

## 순서

```
0. exact interface·상한 확인     command 50과 local default 반영
   │
1. B4 completion 보관            tombstone·overflow 계측 구현
   │
2. W1 scheduler                  두 lane·두 축·claim·time cap 구현
   │
3. W5 관찰자                     latest slot·terminal 보관·loss 구현
   │
4. W2 selector                   RouteMesh·ClientServer plan 구현
   │
5. W6 수신 공정성                코드 경로 반영, 전체 process matrix 대기
   │
6. W4 유휴 정리                  local close 경로 반영, process 증거 대기
   │
7. W3 relay 통지                 .NET wire·relay·conditional invalidation 구현
   │
8. C부 구조 부채                 기능 gate와 별도
```

## B4 — completion table 무음 드롭 (먼저)

`ZLinkMeshCompletionTable`은 early completion과 tombstone을 각각 건수·byte로 제한한다.
early 저장 공간이 가득 차면 tombstone을 남겨 늦게 `Register`한 호출이
`RequestResult.Backpressured`를 받고, request failure mapper가 이를
`CapacityExceeded`로 변환한다. 두 보관 공간까지 가득 차는 경우에는 parts를 해제하고
`OverflowCount`와 `zlink.mesh_node.messages.dropped{reason=capacity_exceeded}`를 기록한다.

이 경로는 callback이 아직 등록되지 않은 completion의 실패를 원래 caller에게 동기적으로
전달할 수 없는 경계를 보완한다. early/tombstone 보관 한도, parts 소유권, register·complete
경쟁과 최종 overflow 계측은 `ServiceRuntimeFoundationTests`에 고정되어 있다.

## W1 — scheduler (가장 큼)

한 단위로 묶는 항목: **A4 · A6 · A8 · A10 · D2 · D3**

`ZLinkSerialExecutionQueue`를 중심으로 제출부터 회계 반납까지 다시 짠다.

| 단계 | 지금 | 목표 |
|---|---|---|
| 한도 | application·lifecycle 각각 count와 `WorkItemFixedCostBytes + payloadBytes`를 예약 | 두 축을 하나의 예약으로 유지 |
| lane | application·lifecycle 두 FIFO lane | lifecycle 우선·32 turn yield debt |
| 선택 | claim generation을 부여한 뒤 10ms owner slice로 drain | slice 종료 후 ready로 재등록 |
| 포화 | closed와 full을 `ShuttingDown`·`CapacityExceeded`로 분리 | 같은 runtime의 public error kind 유지 |
| 반납 | handler terminal completion에서 count·bytes·claim을 함께 반납 | relocation hold는 1024건·16MiB |

**예약 슬롯과 우선 실행은 다른 것이다.** 현재 .NET 구현은 `reservedPrioritySlots`에
의존하지 않고 application·lifecycle queue를 별도 lane으로 둔다. lifecycle이 ready이면
먼저 선택하고 32 turn 뒤 application에 yield debt를 적용하므로 A10의 실행 순서를 직접
표현한다.

**D2 — 수신 회계 lock.** `Runtime/Dispatch/ZLinkInboundDispatchBudget.cs:64,93,99`가 수신·
handler 시작·완료·snapshot에 같은 `_gate`를 쓴다. HWM 판정용 `pendingBytes`만 process 단위
atomic으로 두고 관측 누계는 cache-line 분리 shard로 나눈다. `HandlerStarted`가 HWM 판정에
필요 없다면 metric counter로 분리해 hot path의 세 번째 동기화를 없앤다.

> **worker별 permit 덩어리 예약은 쓰지 않는다.** 쓰지 않은 local 잔량을 pending으로 세면
> HWM보다 일찍 멈추고, 세지 않으면 초과량이 커진다.

**D3 — HWM 초과량 — 결정 완료.** 수신 loop이 용량을 확인한 뒤 message 전체를 받고 **그 다음에야**
크기를 잰다(`Runtime/Channels/ZLinkChannelReceiveLoop.cs:38,41,56`). 현재 .NET binding에는 complete
message 길이를 `Recv` 전에 확인하는 API가 없으므로 binding 변경 대신 bounded receive reservation을
선택한다. `MaxMessageSize = M`, 동시 raw receive 수 `R`에 대해 application pending 상한을
`HWM + R * M`으로 명시하고, HWM 이후 raw receive도 `R`개 reservation 범위에서만 허용한다.
control로 분류된 record는 즉시 reservation을 반환하고 application record는 terminal 상태까지
유지한다. 이 규칙은 common spec과 internals에 반영했으며, W1 회귀 테스트는 `R = 1`과 multipart
경계에서 overshoot가 더 늘지 않는지를 확인한다.

**D4 — 복사.** `Runtime/Messaging/ZLinkMessageRuntime.cs:82`의 binding 경계 payload 복사는
유지한다. relocation accepted record는 queue admission 때 만들지 않고 seal 이후에만
materialize하며, payload는 accepted work item이 소유한다. 따라서 현재 남은 D4는 공개
binding 경계와 deserialize 경로의 전체 copy audit이다.

## W5 — 관찰자 (구조 변경 큼)

항목: **A11 · A12**

두 자리는 `ZLinkObservationQueue`로 합친다. intermediate status는 source별 latest slot에
두고, terminal status는 별도 bounded FIFO에 보관한다. terminal이 가득 차면 가장 오래된
terminal만 버리고 observer별 `ZLinkObservationLoss`와 runtime metric을 함께 갱신한다.

| 위치 | 현재 구현 | 확인 |
|---|---|---|
| `Runtime/Locations/ZLinkFanoutRuntimeService.cs` | latest slot + terminal FIFO + loss envelope | `ZLinkObservationQueue` |
| `Runtime/Channels/ZLinkClientServerRuntimeService.cs` | latest slot + terminal FIFO + loss envelope | `ZLinkObservationQueue` |

`BoundedChannel`의 full mode로는 조항을 만족할 수 없다. **source별 최신 slot + terminal
보관 + 관찰자별 유실 누계** 구조를 새로 만들어야 한다.

전달 형태 `{status, 유실 누계}`는 `ZLinkObservedStatus<T>` exact interface에 반영되어 있다.

관찰자마다 status를 복제하지 말고 불변 snapshot 하나를 공유하고 queue에는 참조와 순번만 넣는다.

## W2 — selector

항목: **A1 · D1**

**per-server DEALER 구조는 유지한다**(A1b 판정 완료). 선택 절차와 topology-change plan만
바꾼다.

| 경로 | 지금 | 할 일 |
|---|---|---|
| RouteMesh 채널 | 전체 후보를 NodeRid로 정렬하고 smooth weighted selection plan을 topology 변경 때 만든다 | send 경로는 plan cursor만 진행 |
| ClientServer | smooth weighted selection plan과 Server RID tiebreak를 사용한다 | connection revision 변경 때 plan 갱신 |

.NET은 RouteMesh와 ClientServer 모두 selection plan으로 전환했으며, 현재 selector 구현
갭은 focused unit evidence 기준으로 남아 있지 않다.

## W6 — 수신 공정성

항목: **A9**

채널 loop은 bounded inbound reservation과 application dispatch queue를 사용하고, RouteMesh
mesh pump는 ready-owner claim·count/byte/time batch·rotation을 사용한다. STREAM은 peer별
receive state를 정렬한 뒤 cursor로 회전하며 multipart 경계와 HWM reservation을 유지한다.
Entry Spot actor lifecycle drain도 같은 batch 상한을 사용한다.

다만 조항 범위가 topology와 binding까지 넓으므로 코드 반영만으로 전체를 증명할 수 없다.
언어×topology process matrix는 별도 검증 조건으로 남긴다.

## W4 — 유휴 정리 (신규 기능)

항목: **A3**

`ZLinkSpotNodeCatalog`가 `InstanceSpotIdleTimeout`을 사용해 후보를 검사하고,
`JoinedActorCount`·relocation 참여·pending application work를 제외한다. 후보는 serial
quiescence 뒤 `IdleEvicted` callback을 호출하고 local activation을 dispose한 뒤 location
record를 release한다. 새 call과의 경쟁은 `_closing` transaction과 serial barrier가 조정한다.
instance Spot process 증거와 cold activation/ordinary message matrix는 별도 검증으로 남긴다.

## W3 — relay 통지

항목: **A2 · D5**

`ZLinkServiceWireCodec`에 command 50 `messageFollow` version/body/fence 검증을 추가하고,
Actor·Spot relay 성공 뒤 source node로 한 번만 통지한다. `ZLinkManagedMeshNode`는 inbound
peer와 source/target generation을 확인한 뒤 `ZLinkStoreLocationResolvers`의 조건부 cache
invalidation을 호출한다. Actor marker는 lease 수명에, Spot marker는 follow 수명에 둔다.
현재 남은 조건은 cross-language mixed-process relay와 one-way/request matrix다.

## C부 구조 부채

application이 관찰할 수 없으므로 우선순위가 가장 낮다.

- relocation 드라이버 3종을 단일 상태기계로
- `Runtime/Service/ZLinkManagedMeshNode.cs`(8,271줄) 분해 — 줄 수가 아니라 책임별 후보로
- `Runtime/Backend/Contracts`의 interface 26개 중 구현이 하나뿐인 것을 이름으로 나열해 정리
- `ZLinkLegacyMonitoringModels` 정리

## 검증

W1은 [2. Spot·Actor 실행 직렬화](../../framework/common/internals/02-serialization.ko.md), W5는
[10. Liveness와 상태 공개](../../framework/common/internals/10-liveness-and-state.ko.md)의
"확인할 결과"를 쓴다. W5는 **느린 관찰자를 인위적으로 만들어 두고** message 처리 속도가
유지되는지, terminal이 보존되는지, 유실 수가 관찰자마다 따로 세어지는지를 함께 본다.

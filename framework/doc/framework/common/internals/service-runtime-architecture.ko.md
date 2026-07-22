# Service runtime architecture

[내부 구조 목차](README.ko.md) · [Framework 개요](../../spec/01-overview.ko.md) ·
[비동기 실행 계약](../../spec/04-async-execution-policy.ko.md)

이 문서는 RouteMesh 11.0 목표 구조를 설명한다. 현재 구현과의 차이와 완료 상태는
`framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md`가 소유한다.

## 1. 책임 경계

Core는 context, message, raw socket, transport, poller, timer와 generic monitor만 제공한다. C++·.NET·JVM·Node.js
Framework runtime은 각각 service protocol, routing, mailbox, completion, Spot·Actor lifecycle, discovery와
maintenance를 구현한다. Java와 Kotlin은 JVM runtime 하나를 공유한다.

```text
+----------------------------------------------------------+
| Application API and language host adapter                |
+----------------------------------------------------------+
| Language service runtime                                 |
| routing | mailbox | owner | completion | monitoring      |
+----------------------------------------------------------+
| Installed public raw binding API                         |
+----------------------------------------------------------+
| Core raw sockets, transport, poller, timer and monitor   |
+----------------------------------------------------------+
```

공통 native Framework runtime, service C ABI와 private C SPI는 두지 않는다. Framework는 설치된 해당 언어
binding의 public package만 사용한다. Binding에 필요한 raw transport 기능이 없으면 binding 공개 계약을 먼저
보완하며 reflection, JNI·N-API 직접 호출, private header와 내부 handle로 우회하지 않는다.

Raw gateway와 mailbox는 service 구현 상세를 흡수한다. 이 내부 결정을 위해 public handler, call object,
builder와 DTO에 transport handle, frame layout과 scheduling option을 추가하지 않는다.

## 2. Runtime aggregate

언어별 이름은 달라도 runtime은 host coordinator, raw transport gateway, protocol codec, topology·peer registry,
mailbox scheduler, operation registry, object runtime, authority coordinator와 observability 책임을 분리한다. Raw
gateway는 protocol이나 object state를 소유하지 않고 protocol codec은 handler를 호출하지 않는다. Store provider는
Framework transfer phase와 transfer envelope을 해석하지 않는다.

Process에는 host runtime aggregate 하나가 존재한다. Aggregate는 여러 RouteMesh, ClientServer, fanout과
STREAM topology를 한 lifecycle로 조정한다. Topology별 runtime은 selection과 monitoring view를
제공하지만 독립 종료 owner가 아니다.

Host aggregate는 다음 소유권을 갖는다.

- immutable registration snapshot과 startup validation result
- raw context와 listener·connector handle
- topology·peer registry와 immutable selection snapshot
- application mailbox와 infrastructure mailbox
- request operation table과 terminal completion
- owner lease, descriptor와 recovery coordinator
- Spot·Actor·Instance activation aggregate
- STREAM session과 Actor binding aggregate
- monitoring snapshot sequence와 observer queue
- shared termination operation과 terminal result

Resource는 만든 aggregate가 닫는다. Application handler, Store provider와 observer가 raw handle이나 reply route를
보유하지 않는다.

Service identity는 raw handle과 분리한다. Host lifecycle, MeshName·NodeRid·node generation, process-local
ChannelName, global ActorId·SpotRid와 ObjectGeneration, physical connection ID, 128-bit operation ID를 서로 다른
identity로 관리한다. `ActorRef`와 `SpotRef`는 current MeshName·NodeRid를 포함한 immutable location snapshot이다.
Physical connection ID, Store revision과 mailbox claim serial은 public DTO에 나타나지 않는다.

## 3. Receive와 dispatch

Raw receive callback은 multipart ownership을 runtime-owned immutable record로 넘긴 뒤 즉시 반환한다. Decode는
magic, protocol capability, field bound와 frame count를 application admission 전에 검증한다. Malformed input은
application handler를 호출하지 않는다. Peer admission은 stable rejection으로, request는 protocol error로,
one-way record는 diagnostic drop으로 끝낸다.

Request operation은 source runtime이 만든 128-bit operation ID로 식별한다. Reply correlation entry는 정확히
한 terminal owner를 가지며 reply, timeout, cancellation, shutdown과 transport failure가 CAS로 경쟁한다.
승리하지 못한 late completion은 application callback을 다시 완료하지 않는다.

One-way submit은 local target queue 또는 source raw transport가 record를 수락한 시점까지만 완료 의미를 가진다.
Remote target queue와 handler 완료 여부는 이 결과로 확인하지 않는다. Request는 reply 또는 닫힌 failure까지
operation table을 유지한다. Runtime은 target handler 실행 여부가 불명확한 request를 새 operation ID로 숨은
재제출하지 않는다.

## 4. Mailbox

Application domain은 handler turn, Spot·Actor admission, timer와 session callback을 처리한다. Infrastructure
domain은 peer admission, send-ready, request completion, lease, transfer recovery, host barrier와 STREAM fence를
처리한다. 두 domain은 bounded queue와 scheduling state를 분리한다.

Application callback이 비동기 대기 중이어도 infrastructure domain은 계속 진행해야 한다. Observer와 metric
exporter는 어느 mailbox의 progress claim도 소유하지 않는다. Queue capacity, pending byte와 active turn은
runtime snapshot의 해당 domain에 투영한다.

## 5. Startup과 종료

Startup은 registration validation, raw capability 구성, bind, recovery, descriptor publish, peer admission과
readiness commit 순서로 진행한다. 모든 required component가 ready인 하나의 snapshot을 commit하기 전에는
application admission을 열지 않는다.

종료는 host maintenance barrier가 소유한다. `Retire`와 `Shutdown`의 차이, first-intent-wins, waiter cancellation,
deadline과 terminal result는 정식 lifecycle spec을 따른다. Topology resource를 따로 닫는 operation이 host
barrier를 우회하지 않는다.

## 6. 구현 검증

- 네 runtime이 동일 schema와 golden frame을 decode하고 같은 terminal result를 만든다.
- Framework public API에 raw handle, wire field와 scheduler 구현 정보가 추가되지 않는다.
- Application callback 대기 중 completion, lease와 termination이 진행된다.
- 한 operation의 terminal completion과 한 host의 termination result가 한 번만 완료된다.
- Raw handle, poll 순서, connection ID와 frame 배열이 application API에 노출되지 않는다.

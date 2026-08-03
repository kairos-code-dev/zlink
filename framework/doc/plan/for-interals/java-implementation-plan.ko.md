# Java framework 구현 계획

[계획 문서 묶음](README.ko.md) · [구현 갭 목록](framework-internals-implementation-gaps.ko.md)

판정과 근거는 [구현 갭 목록](framework-internals-implementation-gaps.ko.md)이 소유한다.
이 문서는 **무엇을 어떤 순서로 고칠지**만 다룬다.

> **여기 적힌 `file:line`은 길잡이다.** 판정의 근거가 아니며, 갭 목록과 어긋나면 **갭
> 목록이 맞다.** 새 사실을 발견하면 갭 목록을 먼저 고친다.

Kotlin은 Java runtime을 공유하므로 별도 작업이 없다.


## 이 언어의 특징

**public error kind enum이 구형 40-kind 모델이다.** 네 언어 중 Java만 다르다. wire는 enum
**이름 문자열**을 보내므로 숫자 충돌은 없지만, 이름이 서로 없어서 상대가 읽지 못한다. 이후
묶음이 전부 이 kind를 쓰므로 먼저 닫는다.

**Spot·Actor queue가 사실상 무한하다.** 기본 capacity가 `Integer.MAX_VALUE`이고 production
경로가 한도 검사 없는 `enqueue`를 쓴다. 한도 도입 자체가 W1의 선행 과제다.

두 채널 종류 모두 framework에서 고르므로 Core 수정에 걸리지 않는다. 실행 자원은
virtual thread per task라 두 실행 영역 분리를 물리적으로 구현할 수 있다.

## 순서

```
0. 선행 판정 대기        상한 값 · exact interface · wire 형식
   │
1. W7 error kind ABI     ★ 가장 먼저. 이후 전부가 이 kind를 쓴다
   │
2. B6 static executor    작고 독립적. runtime 수명 문제
   │
3. W1 scheduler          한도 도입부터. 가장 큼
   │
4. W2 selector           선행 과제 없음
   │
5. W5 관찰자             exact interface 확정 후
   │
6. W6 수신 공정성        fanout은 충족. 나머지 topology 감사
   │
7. W4 유휴 정리          신규 기능
   │
8. W3 relay 통지         wire command 확정 후
   │
9. C부 구조 부채
```

## W7 — error kind ABI (가장 먼저)

항목: **A5**

`errors/ZLinkFrameworkErrorKind.java:3-43`이 40-kind 구형 모델이다.

| 언어 | enum | 값 6 |
|---|---|---|
| C++·.NET·Node | 공통 13-kind | `CapacityExceeded` |
| **Java** | 40-kind | **`SPOT_TYPE_MISMATCH`** |

`CAPACITY_EXCEEDED`가 아예 없고 대신 `WORKER_QUEUE_FULL=17`,
`PLACEMENT_CAPACITY_EXHAUSTED=30`을 쓴다.

**wire는 숫자가 아니라 이름을 보낸다**(`runtime/messaging/ZLinkFrameworkErrorReply.java:19-29,48-60`).
따라서 위험은 값 충돌이 아니라 **이름 불일치**다 — Java가 보내는 `WORKER_QUEUE_FULL`을
다른 언어가 모르고, 다른 언어가 보내는 `CapacityExceeded`를 Java `valueOf()`가 읽지 못한다.

**시작 전에 두 가지를 정한다.**

| 결정 | 선택지 |
|---|---|
| wire 표현 | 공통 숫자로 바꿀 것인가, 공통 이름을 유지할 것인가 |
| 이행 | 구형 이름 alias를 얼마 동안 받아줄 것인가 |

**public enum 교체와 wire 형식 결정은 별개 작업이다.** 전자는 Java 사용자 코드를 깨고,
후자는 언어 간 상호운용을 바꾼다. 섞으면 되돌리기 어렵다.

먼저 **네 언어의 error envelope encode·decode 표**를 만든다. 지금은 Java만 확인했다.

교체 후 매핑을 다시 잡아야 하는 자리도 함께 정리한다 — queue 포화, completion table 부족,
placement 수용량이 각각 어느 kind로 가는지.

## B6 — static executor

`execution/ZLinkAsyncSerialQueue.java:15-18`의 `HANDLER_EXECUTOR`가 JVM 전역 static이고
종료 경로가 없다. 같은 JVM에 runtime이 둘이면 실행 자원을 공유하고, 하나를 닫아도 thread가
남는다.

runtime이 소유하는 executor를 생성자로 주입하고 runtime close가 수명을 소유한다. 작고
독립적이지만 **W1에서 실행 자원 구조를 건드리므로 그 전에 넣는 편이 낫다.**

## W1 — scheduler (한도 도입이 선행)

한 단위로 묶는 항목: **A4 · A6 · A8 · A10 · D2 · D4**

**다른 언어와 출발점이 다르다.** Java는 한도 자체가 없는 상태에서 시작한다.

| 단계 | 지금 | 목표 |
|---|---|---|
| 한도 | **사실상 무한.** Spot context가 기본 생성자를 쓰고 기본값이 `Integer.MAX_VALUE` (`runtime/spots/ZLinkDefaultSpotContext.java:43`, `runtime/spots/ZLinkDefaultInstanceSpotContext.java:33`, `execution/ZLinkAsyncSerialQueue.java:35-42`) | 건수·byte 두 축을 하나의 예약으로 |
| 제출 경로 | production Spot 경로가 `tryEnqueue`가 아니라 **한도 검사 없는 `enqueue`** (`runtime/spots/ZLinkDefaultSpotContext.java:43,148,551-616`) | 한도를 거치는 경로로 전환 |
| lane | 단일 queue. relocation barrier는 앞쪽, teardown은 뒤쪽 (`execution/ZLinkAsyncSerialQueue.java:65-124`, 호출 `runtime/actors/ZLinkActorDispatchSerials.java:115-162`) | application·lifecycle 두 FIFO lane. **종류마다 다르게 처리하지 않는다** |
| 선택 | — | lane 선택 + 연속 실행 상한 + 양보 부채 |
| 점유 | 한 entry가 끝나면 즉시 다음을 잇는다 (`execution/ZLinkAsyncSerialQueue.java:208-223`) | ready owner + claim 시작 시각 + 경과 시간 확인 |
| 반납 | — | handler 종료 후 두 축 동시 반납 |

**off-by-one도 함께.** `:148,160`이 `outstanding > pendingCapacity`라 한도보다 하나 더 받는다.
`>=`로 고치되, production 경로가 `tryEnqueue`를 쓰도록 바꾸지 않으면 효과가 없다.

**barrier가 capacity를 우회한다.** `enqueueBarrierNext`(`:65-94`)가 한도 검사를 건너뛰고 앞에
넣는다. 조항이 요구하는 것은 **별도 한도를 가진 별도 lane**이지 우회가 아니다.

**D2 — 수신 회계 lock.** `runtime/internal/dispatch/ZLinkInboundDispatchBudget.java:71,81,102,119,133`이
같은 monitor를 통과한다. HWM 판정용만 남기고 관측 누계는 분리한다.

**D4 — 복사.** `ZLinkEncodedPayload.java:13,18`이 생성과 `bytes()` 접근에서 각각 복사하고,
`messaging/ZLinkMessage.java:31,50`이 그 복사를 다시 수행한다. 공개 API의 불변성은 유지하되
runtime 내부 소유권 이전에는 복사하지 않는 경로를 따로 둔다.

## W2 — selector

항목: **A1 · D1**

**per-server 연결 구조는 유지한다**(A1b 판정 완료). 선택 절차만 바꾼다.

| 경로 | 지금 | 할 일 |
|---|---|---|
| RouteMesh 채널 | cursor를 weight 합 범위에 넣는 **연속 구간** (`runtime/internal/service/ZLinkServiceTopologyRegistry.java:224-243`) | 누적값 3단계로 교체. 후보 정렬(`:187-192`)이 NodeRid라 tiebreak는 이미 맞다 |
| ClientServer | Server RID 정렬은 맞지만 **연속 구간 cursor** (`runtime/channels/ZLinkChannelSocketRegistry.java:112-159`) | 누적값 3단계로 교체 |

두 곳 모두 tiebreak 키는 이미 옳고 **절차만 바꾸면 된다.** 네 언어 중 수정 범위가 가장
명확하다.

## W5 — 관찰자

항목: **A11 · A12**

`runtime/internal/monitoring/ZLinkStatusPublisher.java:103-135`가 최신 중간 상태를 교체하고
preserve 항목을 남긴다 — **합치기 방향은 이미 맞다.** 네 언어 중 가장 가깝다.

남은 결함은 둘이다.

| 결함 | 위치 |
|---|---|
| 생성자 `capacity`를 저장·적용하지 않아 preserve 항목이 무한히 쌓인다 | `:21-34` |
| terminal 전달 뒤 `onComplete()`로 stream을 닫는다 | `:129-132` |

두 번째는 "가득 찼다는 이유로 종료하지 않는다"와는 다른 위반이다 — terminal을 보냈다고
구독 자체를 끝내면 안 된다. terminal은 source slot만 닫고 구독은 유지한다.

**실행 비용도 함께 본다.** 관찰자마다 virtual thread를 만들고(`:80`) 수요와 무관하게
25ms마다 snapshot·fingerprint를 계산한다(`:14` `POLL_NANOS`, `:103`). 관찰자당 초당 40회다.
polling을 없애고 상태 변경 시 publish하는 구조로 바꾼다.

`spec/server/languages/java/interfaces/monitoring.ko.md`도 전달 envelope에 맞춰 고친다.

## W6 — 수신 공정성

항목: **A9**

fanout은 connection마다 최대 64건으로 **충족**이다
(`runtime/channels/ZLinkFanoutLocationRuntime.java:49,393-404`).

다만 그 `64`는 fanout 전용이고 조항 범위가 RouteMesh·ClientServer·service·STREAM까지
넓어졌다. **부분 확인 상태이며 나머지 경로 감사가 필요하다.** 상한을 건수만이 아니라
건수·byte·경과 시간 셋으로 넓히는 것도 함께 본다.

## W4 — 유휴 정리 (신규 기능)

항목: **A3**

`spots/ZLinkSpotCloseReason.java:3-6`에 `IdleEvicted`를 추가하는 것은 시작일 뿐이다.
Location Store CAS·admission seal·timer 구조가 함께 필요하다.

## W3 — relay 통지

항목: **A2 · D5**

`runtime/actors/ZLinkActorTransferHandoff.java:174-201`이 target 제출 후 회계만 반납한다.
relay 성공 경로(`runtime/actors/ZLinkActorSpotJoinCall.java:948`)에 송신 측 통지가 없고,
캐시 무효화도 stale 오류에만 연결되어 있다(`runtime/actors/ZLinkActorClientRuntime.java:90,143`).
wire command 확정 전에는 시작할 수 없다.

## C부 구조 부채

- `ZLinkOneWayCalls` 중복 5곳 — channels, binding, streams, spots, actors
- `runtime/messaging/`의 세 파일이 `package …runtime.host;`를 선언 —
  `ZLinkAdmissionRuntime.java:1`, `ZLinkOneWayAdmissionStatus.java:1`, `ZLinkOneWayAdmission.java:1`
- 빈 디렉터리 3개 — `systems/zlink/contracts/service`, `runtime/internal/binding`, `runtime/service`

## 검증

W7은 **언어가 섞인 error-reply test**가 완료 조건이다 — Java가 보낸 kind를 다른 언어가
읽고, 그 반대도 되는지 확인한다. 단위 test로는 이름 불일치를 잡을 수 없다. W1은 한도가 없던 상태에서 시작하므로
**빈 payload flood와 큰 payload flood 두 경우**를 모두 확인한다.

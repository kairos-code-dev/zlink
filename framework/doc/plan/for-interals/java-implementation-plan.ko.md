# Java framework 구현 계획

[계획 문서 묶음](README.ko.md) · [구현 갭 목록](framework-internals-implementation-gaps.ko.md)

판정과 근거는 [구현 갭 목록](framework-internals-implementation-gaps.ko.md)이 소유한다.
이 문서는 **무엇을 어떤 순서로 고칠지**만 다룬다.

> **여기 적힌 `file:line`은 길잡이다.** 판정의 근거가 아니며, 갭 목록과 어긋나면 **갭
> 목록이 맞다.** 새 사실을 발견하면 갭 목록을 먼저 고친다.

Kotlin은 Java runtime을 공유하므로 별도 작업이 없다.

## 2026-08-04 현재 판정

Java runtime 수정과 unit/integration test는 통과했다. Core unit test task, Core
`integrationTest`, dispatch pump 단위 test, idle eviction 재활성화 integration test,
Kotlin test가 각각 통과했다. payload-aware queue admission과 cross-node Location Store
CAS 회귀 test를 추가한 뒤에도 전체 Java/Kotlin sample runner가 Java·Kotlin 샘플의
client/server self-check를 완료하고 `All Java/Kotlin samples passed`와 exit 0으로
종료했다.

공통 `framework/doc/framework/common/internals/`와 대조해 앞쪽 삽입,
message 문자열 기반 retry, subscriber별 polling을 사용하지 않는 경로를 확인했고, 수신
경로에는 건수·byte·경과 시간 budget과 rotating cursor를 추가했다. RuntimeMonitoring
aggregate는 10개 scenario 전체 통과가 아니다. 현재 실행에서 A2·A3·A4A·A4B·A5·D1A가
통과했고, A1은 aggregate에서 한 번 실패했으나 단독 실행은 통과했다. B1·B2는 feature-map의
부분 구현 상태이며 D1B는 3초 readiness 제한에서 실패했다.

Java·Kotlin API snapshot과 local Maven package clean consumer는 모두 통과했다. queue의
payload admission surface를 반영한 Java snapshot은 2,852 lines, Kotlin snapshot은
3,360 lines이며 source/package hash가 일치한다. 전체 sample runner도 Java·Kotlin
client/server self-check를 끝냈다. 반면
`validate_sample_e2e_configuration_policy.sh`는 기존 E2E application의 `System.getenv`·
`System.getProperty` 사용으로 실패했다. 이 gate는 sample runner의 runtime 동작과 별도의
configuration policy 조건이며, 해당 E2E 구성 이행은 이번 Java runtime 변경으로 완료되지 않았다.

현재 Java는 service-wire schema와 generated constants의 `messageFollow` command 50을
generic codec, admitted-peer 뒤의 infrastructure receive, M6B route fence, relay success,
source route cache invalidation까지 연결했다. relocation 시 보관하는 target route에는
object generation, target node generation, authority owner generation, owner lease
generation을 저장하고, relay notice에는 original operation과 reply route ID를 보존한다.
이 범위는 Java unit/integration test와 전체 Java/Kotlin sample runner로 확인했다.

다만 mixed-language process gate와 전체 topology matrix는 아직 실행하지 않았다. 또한
notice 중복 억제 표식의 공통 수명 정책(D5), queue·batch 기본값의 언어 간 측정(D6),
legacy error packet의 공통 encode/decode는 별도 조건으로 남긴다.

이 결과는 Java runtime·unit/integration·package consumer·sample과 Java 내부
message-follow 경로까지의 승인 근거이지 Java implementation plan 전체 완료 판정은 아니다.
legacy error packet의 공통 service-wire 연결과 언어 간 error-reply test, idle eviction의
cross-node generation 정책, mixed-language message-follow process test, 전체 topology
수신 process matrix, D4 내부 소유권 회계와 D6 측정값은 아직 남아 있다.


## 이 언어의 특징

**public error kind enum은 공통 13-kind 모델로 교체했다.** 공통 service-wire schema도
`framework-error-code`를 `u32` 값으로 정의한다. 다만 legacy Java error packet은 이 service
record를 사용하지 않고, 다른 언어의 error envelope와 함께 encode·decode 경로가 연결되지
않았으므로 interoperability closure는 아직 남아 있다.

**Spot·Actor queue는 runtime-owned bounded queue로 연결했다.** 건수·byte 두 축과 owner
time budget을 적용하고, Channel·Mesh·STREAM·Spot·Actor 수신 경계가 역직렬화 전에
payload byte를 admission에 전달한다. 고정 비용과 공통 기본값은 D6으로 남아 있다.

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

`errors/ZLinkFrameworkErrorKind.java`는 공통 13-kind 모델을 사용한다.

| 언어 | enum | 값 6 |
|---|---|---|
| C++·.NET·Node | 공통 13-kind | `CapacityExceeded` |
| **Java** | 공통 13-kind | **`CAPACITY_EXCEEDED`** |

구형 kind를 public surface에서 제거하고 queue·placement·runtime 오류 매핑을 공통 kind로
맞췄다. 관련 unit test와 sample 호출도 갱신했다.

`ZLinkFrameworkErrorReply`는 아직 enum 이름을 사용하는 legacy internal packet이다. 공통
schema의 숫자 error code 정의만으로 이 packet이 자동으로 상호운용되는 것은 아니다. C++,
.NET, Node의 channel envelope와 함께 공통 encode·decode 경로 및 legacy 이행 규칙을
검증할 때까지 interoperability closure는 승인하지 않는다.

**남은 결정은 두 가지다.**

| 결정 | 선택지 |
|---|---|
| legacy packet 연결 | 공통 `framework-error-code` record로 통합할 것인가 |
| 이행 | 구형 이름 packet을 얼마 동안 alias로 받아줄 것인가 |

**public enum 교체와 legacy packet 이행은 별개 작업이다.** 전자는 완료되었고 공통
schema의 숫자 code도 정의되었지만, 후자는 언어별 encode·decode와 cross-language test가
통과할 때까지 미완료로 남긴다.

완료 조건은 네 언어의 error envelope encode·decode 표와 언어가 섞인 error-reply test다.

교체 후 매핑을 다시 잡아야 하는 자리도 함께 정리한다 — queue 포화, completion table 부족,
placement 수용량이 각각 어느 kind로 가는지.

## B6 — static executor

`execution/ZLinkAsyncSerialQueue.java`의 전역 executor를 제거했다. registration이
runtime-owned virtual-thread executor를 만들고 actor·channel·spot·stream queue에 주입하며,
runtime close가 handler executor와 serial executor를 모두 종료한다. one-way admission의
deadline scheduler도 backend source가 소유하고 source shutdown에서 종료한다.

**판정 — 해소.** production static executor 검색과 Core unit test로 확인했다.

## W1 — scheduler (한도 도입이 선행)

한 단위로 묶는 항목: **A4 · A6 · A8 · A10 · D2 · D4**

**현재 Java는 두 축 queue를 production 경로에 연결했다.** 일반 payload는 알려진 길이를
admission에 넘기고 빈 payload와 lifecycle 작업은 고정 비용으로 처리한다. 기본값의 공통
측정·판정은 아직 남아 있다.

| 단계 | 지금 | 목표 |
|---|---|---|
| 한도 | queue마다 건수·byte 두 축을 함께 예약하고 active entry도 포함한다 (`execution/ZLinkAsyncSerialQueue.java`) | 기본값과 payload byte 경계의 공통 판정 |
| 제출 경로 | Spot·Actor·channel·stream production queue가 runtime-owned executor와 bounded queue를 사용한다 | 모든 public 결과의 capacity mapping 점검 |
| lane | application·lifecycle FIFO와 continuation re-entry FIFO를 분리하고 앞쪽 삽입을 사용하지 않는다 | 공통 lane 선택 규칙과 양보 부채 경계 |
| 선택 | lifecycle burst limit으로 application starvation을 제한한다 | 공통 기본값 측정 |
| 점유 | claim 시작 시각과 owner time budget을 확인하고 상한 시 executor에 재등록한다 | D6 값 확정 |
| 반납 | handler 완료 시 lane별 건수·byte를 반납한다 | 공통 기본값 측정 |

`canReserve`가 두 lane의 건수·byte를 원자적으로 확인한 뒤 예약하므로 active entry를 포함한
한도는 queue admission에서 적용된다.

barrier도 lifecycle lane의 별도 한도에 걸리며 FIFO 뒤에 추가된다.

**D2 — 수신 회계 lock.** Java는 HWM permit과 completion 회계를 분리했다. 관련 unit test가
통과했으며 다른 언어의 lock 판정은 갭 목록에 남아 있다.

**D4 — 복사.** `ZLinkEncodedPayload`는 공개 불변성을 위해 생성과 `bytes()` 접근에서
방어적 복사를 한다. 반면 `ZLinkMessage.fromEncoded`는 전달된 `ZLinkEncodedPayload` 객체를
그대로 보관하고 `ZLinkMessageOwnershipTest`가 decode·re-encode에서 같은 객체를 확인한다.
다만 empty 판정과 serializer 경로는 `bytes()`를 호출하므로 모든 내부 hot path가 zero-copy인
것은 아니다. 공개 경계를 훼손하지 않는 내부 소유권 이전 경로와 buffer 복사 회계는 D4에
남긴다.

## W2 — selector

항목: **A1 · D1**

**per-server 연결 구조는 유지한다**(A1b 판정 완료). 선택 절차만 바꾼다.

| 경로 | 지금 | 할 일 |
|---|---|---|
| RouteMesh 채널 | 후보 current를 갱신하는 smooth weighted round-robin | `M6ARuntimeContractTest`에서 누적 선택과 NodeRid tiebreak를 검증 |
| ClientServer | Server RID 정렬을 유지한 smooth weighted round-robin | `ZLinkClientServerM6RuntimeTest`에서 선택 순서와 세대를 검증 |

두 곳 모두 tiebreak 키는 이미 옳고 **절차만 바꾸면 된다.** 네 언어 중 수정 범위가 가장
명확하다.

## W5 — 관찰자

항목: **A11 · A12**

`runtime/internal/monitoring/ZLinkStatusPublisher.java`는 최신 중간 상태를 source별로
합치고 preserve 항목을 보존한다. terminal 전달 뒤 stream을 닫지 않으며, 전달 envelope에
coalesced·discarded terminal 누계를 넣는다. runtime-owned SignalHub가 source를 한 번
감시하고 subscriber는 hub signal만 받는다.

`spec/server/languages/java/interfaces/monitoring.ko.md`와 exact snapshot도 envelope에
맞췄고, publisher·Spring·RuntimeMonitoring sample test를 통과했다.

## W6 — 수신 공정성

항목: **A9**

공통 `ZLinkReceiveBatchBudget`은 message 64건, payload 1 MiB, 경과 시간 2 ms 중 먼저
도달한 조건으로 멈춘다. Channel request/subscribe/route, Spot·Entry·Instance activation,
ClientServer control, fanout, STREAM, raw mesh receive 경로에 연결했다. fanout과 STREAM은
정렬된 connection 목록과 rotating cursor를 사용하고, `ZLinkJavaMeshDispatchPump`도
2 ms에 dispatch turn을 끝내 pending domain을 다음 scheduled task로 넘긴다.

`ZLinkSpotRouteBridgeDrainer`는 채널 순서를 회전시키지만 backend `drain()`이 처리 건수만
반환해 byte·시간 상한을 내부에서 확인할 수 없다. 현재 Java production route bridge는
직접 raw/mesh 경로를 사용해 이 bridge가 활성화되지 않는다. 따라서 공통 internals가 요구한
전체 topology matrix와 실제 multi-process 공정성은 **부분 확인**으로 남긴다.

## W4 — 유휴 정리 (신규 기능)

항목: **A3**

`ZLinkInstanceSpotActivation`은 마지막 활동 시각, active route receive, timer,
quiescence를 확인한 뒤 유휴 정리를 시작한다. `ZLinkSpotRuntime`은 Location Store의
READY→CLOSING CAS를 먼저 수행하고, 같은 시점에 route cache admission을 봉인한다. 이후
closing callback, 조건부 authority delete, local resource 정리가 이어진다.

`InstanceSpotRuntimeIntegrationTest.publicRequestReactivatesInstanceSpotAfterIdleEviction`
은 authority 삭제 뒤 일반 `requestToSpot`이 `NOT_FOUND`가 되고,
`instanceSpot("EchoInstance")` 명시 호출이 새 instance를 활성화하는 흐름을 확인한다.
두 번의 초기화와 `IDLE_EVICTED` 종료 사유도 확인한다. 다만 공통 wire와
cross-node generation 경쟁 정책은 별도 계약이 없어 Java 계획의 공통 완료 조건으로는
남겨 둔다.

## W3 — relay 통지

항목: **A2 · D5**

Java는 command 50 codec과 known-command 판정, admitted peer 뒤의 raw infrastructure
receive, M6B relay success, source node 통지, exact route-fence cache invalidation을
owner layer에 연결했다. `ZLinkActorTransferHandoff`는 relocation 시 target route와
Message Follow queue snapshot을 보관하고, relay는 Store를 다시 조회하지 않고 이 값을
사용한다. target route가 없거나 public `ActorRef`와 backend actor reference의 공통
필드가 일치하지 않으면 source handoff를 설치하지 않는다.

이 변경은 `ZLinkServiceMessageFollowWireCodecTest`,
`ZLinkJavaRawMeshNodeM6ATest`, `ZLinkJavaRawSpotNodeM6BTest`,
`ZLinkActorTransferHandoffTest`, `ZLinkStoreLocationResolversTest`, 전체 Java core
unit/integration test와 sample runner로 확인했다. 남은 조건은 mixed-language process
test, 전체 topology matrix, 그리고 D5 표식 수명과 D6 공통값의 cross-language 판정이다.

## C부 구조 부채

- `ZLinkOneWayCalls`는 `runtime/internal/calls/`의 공통 구현으로 통합했다.
- `ZLinkAdmissionRuntime`, `ZLinkOneWayAdmissionStatus`, `ZLinkOneWayAdmission`은
  `runtime/host/` package와 실제 경로를 일치시켰다.
- `runtime/internal/binding`과 `runtime/service` 빈 디렉터리는 제거했다. 생성 계약 namespace인
  `systems/zlink/contracts/service` 빈 디렉터리는 남아 있어 C부 전체를 완료로 판정하지 않는다.

## 검증

W7은 **언어가 섞인 error-reply test**가 완료 조건이다 — Java가 보낸 kind를 다른 언어가
읽고, 그 반대도 되는지 확인한다. 단위 test로는 이름 불일치를 잡을 수 없다. W1은 현재
건수·byte 예약, 고정 비용과 일반 payload 길이 전달 경로를 확인했지만 공통 기본값은 D6으로
남으므로 **빈 payload flood와 큰 payload flood**를 각각 확인해야 한다. 전체 sample runner는
변경 후 Java/Kotlin process self-check를 통과했지만, 그것이 message-follow wire나 전체
topology fairness matrix를 대신하지 않는다.

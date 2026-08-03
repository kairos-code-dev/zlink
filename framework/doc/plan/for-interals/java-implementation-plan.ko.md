# Java framework 구현 계획

[계획 문서 묶음](README.ko.md) · [구현 갭 목록](framework-internals-implementation-gaps.ko.md)

판정과 근거는 [구현 갭 목록](framework-internals-implementation-gaps.ko.md)이 소유한다.
이 문서는 **무엇을 어떤 순서로 고칠지**만 다룬다.

> **여기 적힌 `file:line`은 길잡이다.** 판정의 근거가 아니며, 갭 목록과 어긋나면 **갭
> 목록이 맞다.** 새 사실을 발견하면 갭 목록을 먼저 고친다.

Kotlin은 Java runtime을 공유하므로 별도 작업이 없다.

## 2026-08-04 현재 판정

Java runtime 수정, unit test, RuntimeMonitoring sample 동작까지 확인했다. Core module
unit test 750건, Spring starter test, Kotlin test가 통과했고, RuntimeMonitoring sample의
10개 scenario도 통과했다. 공통 `framework/doc/framework/common/internals/` 문서와 대조해
앞쪽 삽입·message 문자열 기반 retry·subscriber별 polling을 제거하거나 분리했다.

이 결과는 요청한 runtime·unit·sample 범위의 승인 근거다. Java implementation plan 전체가
완료된 것은 아니다. relay notification wire command, idle eviction의 admission seal과
generation 경쟁 규칙, 전체 topology 수신 공정성, cross-language error wire, D4/D6은
공통 설계 또는 추가 검증이 필요하다.


## 이 언어의 특징

**public error kind enum은 공통 13-kind 모델로 교체했다.** 다만 legacy Java error packet과
다른 언어의 error envelope 표현은 아직 공통 wire schema로 고정되지 않았다.

**Spot·Actor queue는 runtime-owned bounded queue로 연결했다.** 건수·byte 두 축과 owner
time budget을 적용하지만, 고정 비용과 일반 payload byte 회계의 공통 값은 D6으로 남아 있다.

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

`ZLinkFrameworkErrorReply`는 아직 enum 이름을 사용하는 legacy internal packet이다. C++,
.NET, Node의 channel envelope 표현도 서로 다르므로 공통 wire schema가 정해질 때까지
interoperability closure는 승인하지 않는다.

**남은 결정은 두 가지다.**

| 결정 | 선택지 |
|---|---|
| wire 표현 | 공통 숫자로 바꿀 것인가, 공통 이름을 유지할 것인가 |
| 이행 | 구형 이름 alias를 얼마 동안 받아줄 것인가 |

**public enum 교체와 wire 형식 결정은 별개 작업이다.** 전자는 완료되었지만, 후자는
공통 schema와 cross-language test가 정해질 때까지 미완료로 남긴다.

완료 조건은 네 언어의 error envelope encode·decode 표와 언어가 섞인 error-reply test다.

교체 후 매핑을 다시 잡아야 하는 자리도 함께 정리한다 — queue 포화, completion table 부족,
placement 수용량이 각각 어느 kind로 가는지.

## B6 — static executor

`execution/ZLinkAsyncSerialQueue.java`의 전역 executor를 제거했다. registration이
runtime-owned virtual-thread executor를 만들고 actor·channel·spot·stream queue에 주입하며,
runtime close가 handler executor와 serial executor를 모두 종료한다.

**판정 — 해소.** production static executor 검색과 Core unit test로 확인했다.

## W1 — scheduler (한도 도입이 선행)

한 단위로 묶는 항목: **A4 · A6 · A8 · A10 · D2 · D4**

**현재 Java는 두 축 queue를 production 경로에 연결했다.** 기본값과 payload 회계의 공통
판정은 아직 남아 있다.

| 단계 | 지금 | 목표 |
|---|---|---|
| 한도 | queue마다 건수·byte 두 축을 함께 예약하고 active entry도 포함한다 (`execution/ZLinkAsyncSerialQueue.java`) | 기본값과 payload byte 경계의 공통 판정 |
| 제출 경로 | Spot·Actor·channel·stream production queue가 runtime-owned executor와 bounded queue를 사용한다 | 모든 public 결과의 capacity mapping 점검 |
| lane | application·lifecycle FIFO와 continuation re-entry FIFO를 분리하고 앞쪽 삽입을 사용하지 않는다 | 공통 lane 선택 규칙과 양보 부채 경계 |
| 선택 | lifecycle burst limit으로 application starvation을 제한한다 | 공통 기본값 측정 |
| 점유 | claim 시작 시각과 owner time budget을 확인하고 상한 시 executor에 재등록한다 | D6 값 확정 |
| 반납 | handler 완료 시 lane별 건수·byte를 반납한다 | payload 크기 전달 경계 |

`canReserve`가 두 lane의 건수·byte를 원자적으로 확인한 뒤 예약하므로 active entry를 포함한
한도는 queue admission에서 적용된다.

barrier도 lifecycle lane의 별도 한도에 걸리며 FIFO 뒤에 추가된다.

**D2 — 수신 회계 lock.** Java는 HWM permit과 completion 회계를 분리했다. 관련 unit test가
통과했으며 다른 언어의 lock 판정은 갭 목록에 남아 있다.

**D4 — 복사.** `ZLinkEncodedPayload.java:13,18`이 생성과 `bytes()` 접근에서 각각 복사하고,
`messaging/ZLinkMessage.java:31,50`이 그 복사를 다시 수행한다. 공개 API의 불변성은 유지하되
runtime 내부 소유권 이전에는 복사하지 않는 경로를 따로 둔다.

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

fanout은 connection마다 최대 64건으로 충족이다
(`runtime/channels/ZLinkFanoutLocationRuntime.java:49,393-404`).

다만 그 `64`는 fanout 전용이고 조항 범위가 RouteMesh·ClientServer·service·STREAM까지
넓어졌다. **부분 확인 상태이며 나머지 경로 감사가 필요하다.** 상한을 건수만이 아니라
건수·byte·경과 시간 셋으로 넓히는 것도 함께 본다.

## W4 — 유휴 정리 (신규 기능)

항목: **A3**

`spots/ZLinkSpotCloseReason.java`에 `IDLE_EVICTED=3`과 timeout 표면은 추가했지만, 실제
eviction은 시작일 뿐이다.
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

# Framework spec 확장 제안

`framework/doc/framework/common/internals/`를 실제 구현 기준으로 재작성하면서, **정식
spec에 근거가 없는데 application이 관찰할 수 있는 동작**을 12건 발견했다. internals가
임의로 정할 수 없는 것들이므로 여기 모아 spec 개정 판단을 요청한다.

**상태: 12건 모두 정식 spec 문구에 반영했다.** 다만 **문구 반영 = 계약 완결이 아니다.**
일부 항목은 언어별 exact interface 반영과 경쟁 조건 정의가 남아 있다.

| 항목 | 남은 것 |
|---|---|
| 3 `IdleEvicted` | 네 언어 exact interface에 enum 값·timeout 설정 없음. admission seal, owner record CAS, callback 실패 처리 미정 |
| 4 두 축 한도 | 기본값·허용 범위·작업당 고정 비용 미정. exact interface는 아직 "payload byte"라고 적혀 있다 |
| 6·8 상한들 | 값·단위·기본값 미정(측정 후 판정) |
| 11·12 관찰자 | 전달 envelope(status + 유실 누계)가 exact interface에 없음 |

남은 것은 [구현 갭 목록](framework-internals-implementation-gaps.ko.md)이 추적한다.
각 항목의 원래 근거와 대안 검토는 그대로 남겨 둔다.

근거 조사와 판정 기준은 [계획 문서 묶음](README.ko.md)에 있다.

## 판정 기준

각 항목은 다음 질문을 통과한 것만 담았다.

> **두 언어 구현이 이것을 다르게 정하면 application이 알아챌 수 있는가?**

알아챌 수 있는데 spec에 없으면, 구현마다 달라지고 그 차이를 아무도 위반이라 말할 수
없다. 반대로 알아챌 수 없는 것(스레드 구조, 자료구조 선택 등)은 여기 없다 — 그건
internals가 정한다.

## 제안 목록

| # | 항목 | 반영 위치 | 반영 내용 |
|---|---|---|---|
| 1 | 대상 선택 순서를 보장할 것인가 | `08-channel-messaging` §선택 순서 | 누적 weight 3단계 절차 + **topology별 식별자** tiebreak(RouteMesh=NodeRid, ClientServer=Server RID) |
| 2 | 이동 후 위치 캐시 갱신 알림 | `18-object-routing` | 무효화 표에 relay 통지 추가 |
| 3 | 조용한 객체 정리(idle) | `11-spot-model` | `IdleEvicted` 종료 사유 + Instance Spot 한정 조건 |
| 4 | 실행 대기열 한도의 축 | `06-framework-api` | 건수·byte 두 축 의무화, 작업당 고정 비용 합산, 두 축 원자 예약, handler 완료 후 동시 반납(실행 중 작업도 셈) |
| 5 | 로컬 자원 고갈 오류 kind | `32-framework-error-model` | `CapacityExceeded`를 request kind 목록에 추가 |
| 6 | owner 점유 시간 상한 | `06-framework-api` | 연속 점유 시간 상한 + 부분 drain 명시 |
| 7 | dispatch 깨우기 지연 하한 | `06-framework-api` | 도착 기반 wakeup 계약 |
| 8 | 수신 batch 공정성 | `29-transport-liveness` | 한 peer의 수신 독점이 다른 peer 판정에 영향 주지 않음 |
| 9 | lifecycle queue와 업무 queue 우선순위 | `14-actor-model` §3 | lifecycle queue 우선 실행 |
| 10 | Spot 대기열 포화 시 동작 | `12-spot-messaging` §5.3 | 계열×위치 표 — send는 대기 후 `DeadlineExceeded`, request는 local `CapacityExceeded`/remote `Unavailable`, control claim은 별도 한도 |
| 11 | 관찰자 자리 넘침 시 구독 종료 | `24-runtime-monitoring` | source별 최신 slot 합치기, terminal 비덮어쓰기, 보관 상한 초과 시 오래된 terminal 폐기 + 관찰자별 유실 counter, stream 미종료 |
| 12 | 합쳐진 알림의 누계 보존 | `24-runtime-monitoring` | 누계 field는 합친 뒤에도 최신 값 반영 |

---

## 1. 대상 선택 순서를 보장할 것인가

**지금.** `08-channel-messaging:122-123`이 장기 비율만 보장하고 **"개별 호출마다 이 순서를
보장한다는 뜻은 아니다"** 라고 명시한다. 반면 `09-client-server-channel:242-243`은
ClientServer 경로에서 **"같은 weight를 가진 Server끼리는 순환하며 선택한다"** 를 요구한다.

**문제.** 두 문장이 같은 축을 서로 다르게 규정한다. 그리고 네 구현이 실제로 갈렸다 —
매끄러운 가중 순환, 가중 링을 서로소 보폭으로 순회, 커서 나머지 연산, **가중 무작위**.
마지막은 순환을 보장하지 못하므로 ClientServer 요구를 만족하지 않는다.

**관찰 가능한 것.** 같은 후보 집합에 같은 요청을 연속으로 보냈을 때의 선택 순서. 부하
분산 문제를 재현하거나 언어 간에 비교할 수 없다.

**제안.** 둘 중 하나를 고른다.

- **(A) 순서를 계약으로 만든다** — 선택 절차를 spec에 고정한다. 예: 후보마다 누적값을
  두고 `누적값 += weight` → 최대값 선택(동점은 node RID 오름차순) → `선택된 누적값 -=
  weight 합`. 이러면 `08:122-123`의 비보장 문장을 걷어내고 재현 가능한 순서가 계약이 된다.
- **(B) 비보장을 유지한다** — 대신 `09:242-243`의 순환 요구를 "같은 weight 후보가 장기적으로
  균등하게 선택된다"로 완화해 두 문장의 충돌을 없앤다. 무작위 구현이 합법이 된다.

**권고: (A).** 진단 가능성이 크게 다르고, 순환 요구가 이미 있으므로 (B)는 기존 계약을
약화시킨다.

## 2. 이동 후 위치 캐시 갱신 알림

**지금.** 객체가 이동하면 보낸 쪽 캐시는 옛 owner를 가리킨다. `21-location-runtime:691-701`의
Message Follow가 그 message를 새 owner에게 넘긴다. 그러나 **보낸 쪽에 새 위치를 알려 주는
경로가 없다.** `18-object-routing:98`의 캐시 무효화 조건은 닫힌 집합이고 relay 알림이 없다.

**문제.** 캐시가 만료될 때까지 그 객체로 가는 **모든 message가 한 홉을 더 거친다**(최대
8홉). 이동이 잦으면 홉이 쌓인다.

**관찰 가능한 것.** 이동 직후 구간의 지연, 그리고 wire를 오가는 record 집합.

**제안.** 응답을 기다리는 호출은 응답에 새 위치를 실어 보낼 수 있다. 그러나 **응답을
기다리지 않는 호출에는 실을 곳이 없으므로 새 비요청 record가 필요하다.** 이것은 wire
계약 변경이다.

`18-object-routing` §2.2 무효화 조건표에 "relay를 거쳤다는 통지를 받으면 무효화한다"를
추가하고, 그 통지의 형식과 전송 조건을 `service-wire` schema에 정의한다.

**대안.** 알림을 만들지 않고 캐시 수명만으로 수렴시킨다. 구현은 단순하지만 이동 직후
성능 저하 구간이 캐시 수명만큼 남는다.

## 3. 조용한 객체 정리(idle)

**지금.** **네 구현 모두 조용한 Spot·Actor를 정리하지 않는다.** 마지막 활동 시각을
기록하는 곳도, 주기적으로 훑는 곳도 없다. 객체는 소유권을 잃거나 명시적으로 없앨 때만
사라진다. 즉 **한 번 만들어진 객체는 process가 살아 있는 한 남는다.**

**문제.** 방을 계속 만드는 서비스는 메모리가 단조 증가한다. 상한이 있는 구현도 그 값을
**배치를 고를 때만** 쓰고 이미 만들어진 객체를 줄이지 않는다.

**막는 것 두 가지.**

첫째, `11-spot-model:198-200`의 closing reason이 `ExplicitClose`·`HostShutdown`·
`RelocationOut` **닫힌 집합**이다. idle 정리를 추가하려면 새 reason이 필요하다.

둘째, 더 큰 문제 — `18-object-routing:113-119`에 따르면 **Instance intent를 명시한 호출만**
없는 객체를 활성화한다. 따라서 **정리된 User Spot으로 온 일반 message는 되살아나지 않고
실패한다.** "정리했다가 다시 오면 되살린다"가 User Spot에서 성립하지 않는다.

**제안.** 둘 중 하나.

- **(A) Instance Spot으로 한정한다** — Instance Spot만 정리 대상으로 하고, closing reason에
  `IdleEvicted`를 추가한다. User Spot은 명시적 수명 관리에 맡긴다.
- **(B) User Spot도 포함한다** — 정리된 User Spot에 대한 재활성화 규칙을 `18` §3에 새로
  정의해야 한다. 일반 message가 정리된 객체를 되살릴 수 있게 되므로 §113-119의 원칙이
  흔들린다.

**권고: (A).** (B)는 "일반 message는 hidden create를 하지 않는다"는 기존 원칙과 충돌한다.

정리 기준도 함께 정한다 — 마지막 활동 이후 경과 시간과 **진행 중인 작업이 없음**을 함께
만족해야 한다. 시간만 보면 오래 기다리는 작업이 있는 객체를 지운다.

## 4. 실행 대기열 한도의 축

**지금.** `06-framework-api:411`이 "bounded mailbox"만 요구하고 **무엇으로 재는지는 정하지
않는다.** 축이 명시된 곳은 모두 건수와 byte를 함께 쓴다(`28-graceful-drain-handoff:804-805`
이동 중 보류 1,024건/16 MiB, `18-object-routing:133`).

**문제.** 구현이 건수만으로 제한하면 같은 4,096건이 400 KB일 수도 4 GB일 수도 있다.
메모리가 1만 배 차이 나는데 한도는 똑같이 걸린다. 실제로 두 구현의 Spot 대기열이 사실상
무제한이고(기본값이 정수 최대값, 깊이를 세지 않는 연결 구조), 나머지 둘은 건수만 센다.

**관찰 가능한 것.** 어떤 send가 `CapacityExceeded`로 끝나는지.

**제안.** `06-framework-api` §11에 실행 대기열 한도가 **건수와 byte를 함께** 가지며 먼저
걸리는 쪽을 적용한다고 명시한다. 이동 중 보류 한도와 같은 모양이므로 기존 선례와
일관된다.

## 5. 로컬 자원 고갈 오류 kind

**지금.** `32-framework-error-model:66-74`의 Request 종결 kind 목록이 닫혀 있는데 **로컬
bounded resource 고갈을 표현할 kind가 없다.** `CapacityExceeded`는 목록에 없다.

**문제.** 응답 보관 자리가 가득 찼을 때 같은 상황에서 구현이 `Unavailable`을 오용하거나
`InternalFailure`로 가는 등 갈린다. `28:805`에 relocation hold 전용 선례가 있을 뿐이다.

**관찰 가능한 것.** caller가 받는 ErrorKind.

**제안.** `32-framework-error-model` §5 Request 목록에 `CapacityExceeded`를 추가하고, 그
의미를 "로컬 bounded resource를 확보하지 못해 종결"로 정의한다.

## 6. owner 점유 시간 상한

**지금.** `06-framework-api:413`이 "ready owner의 bounded mailbox를 **drain**하고"라고만 한다.
한 owner가 실행 자원을 얼마나 오래 잡을 수 있는지는 정하지 않는다.

**문제.** 짧은 작업이 계속 도착하면 한 owner가 실행 자원을 계속 점유할 수 있다.

**관찰 가능한 것.** 같은 node의 다른 owner가 겪는 최대 대기 시간.

**제안.** `06-framework-api` §11의 "drain"을 부분 drain으로 고치고, owner 하나가 연속으로
점유할 수 있는 상한(시간 기준)을 계약에 넣는다.

## 7. dispatch 깨우기 지연 하한

**지금.** spec에 dispatch wakeup 규정이 없다(`05:281`은 timer 한정).

**문제.** 한 구현이 **1 ms 주기 폴링**이다. 부하와 무관하게 지연 하한이 생기고, 유휴
상태에서도 초당 1,000회 깨어난다. 다른 구현은 도착 즉시 깨어난다.

**관찰 가능한 것.** message 하나의 최선 지연.

**제안.** dispatch가 도착 기반으로 깨어나야 함을 명시하거나, 폴링을 허용하되 **주기
상한을 계약으로 정하고 그 값을 공표**하게 한다. 후자가 현실적이다 — 이벤트 루프 하나로
도는 언어는 막을 수 없다.

## 8. 수신 batch 공정성

**지금.** 한 연결에서 연속으로 얼마나 읽을 수 있는지 규정이 없다.

**문제.** 한 peer가 계속 보내면 그 연결이 수신 단계를 독점할 수 있고, 그 사이 다른 peer의
확인 신호가 밀린다. `29-transport-liveness:141-146`은 15초 침묵을 not-ready 판정으로
규정하며 **"이 판정은 오탐이 아니다"** 라고 못 박는다. 즉 수신 독점이 **다른 peer를
죽은 것으로 만들 수 있다.**

**관찰 가능한 것.** 다른 peer의 ready/not-ready 상태.

**제안.** `29-transport-liveness`에 peer별 수신 공정성이 liveness 판정에 영향을 주지
않아야 한다는 계약을 추가한다.

## 9. lifecycle queue와 업무 queue 우선순위

**지금.** `14-actor-model:615`가 lifecycle 전용 queue를, `:622`가 Actor FIFO를 요구하지만
**둘의 상대 우선순위는 정하지 않는다.**

**관찰 가능한 것.** join·leave 전후로 payload가 처리되는 순서.

**제안.** `14-actor-model` §3에 우선순위 규칙을 추가한다.

## 10. Spot 대기열 포화 시 동작

**지금.** worker queue 포화는 `05-async-execution-policy:54`가 `CapacityExceeded`로
정의한다. **Spot application queue 포화는 규정이 없다.**

**관찰 가능한 것.** 대기인지 실패인지, 실패라면 오류 kind.

**제안.** `12-spot-messaging` 또는 `32-framework-error-model`에 고정한다.

## 11. 관찰자 자리 넘침 시 구독 종료

**지금.** `24-runtime-monitoring:213-220`은 중간 상태 합치기와 **application 주도 취소**만
규정한다. 자리가 넘칠 때 framework가 구독을 끊을 수 있는지는 없다.

**관찰 가능한 것.** 구독이 끊기는지 여부와 그 통지.

**제안.** `24-runtime-monitoring` §3에 "합치기만 하며 stream을 종료하지 않는다"를
명시하거나, 종료 조건과 통지 방식을 정의한다.

## 12. 합쳐진 알림의 누계 보존

**지금.** `24-runtime-monitoring:215-217`의 보장 목록이 셋뿐이고 밀림·버림 카운터 증가분이
없다. 다만 `:209` "각 항목은 완전한 status"이므로 누계가 status에 실려 오면 사실상
충족된다.

**제안.** `24-runtime-monitoring` §3에 "누계 field는 합치기에도 최신 값을 반영한다"를 한 줄
명시하면 닫힌다.

---

## 처리 방침

12건 모두 정식 spec에 반영했으므로 internals 문서는 이 항목들을 **결정으로 쓸 수 있다.**
반영 이전에 "spec 미정의"로 표시했던 문장은 spec 근거 링크로 교체한다.

반영으로 **구현 갭이 새로 생긴 항목**이 있다. spec이 없던 동안 각 구현이 임의로
정했던 동작이 이제 위반이 되기 때문이다. 언어별 수정 목록은
[구현 갭 목록](framework-internals-implementation-gaps.ko.md)에 있다.

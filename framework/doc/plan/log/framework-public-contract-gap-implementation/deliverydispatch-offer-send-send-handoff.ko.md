# DeliveryDispatch 배송 제안 send/send 전환 — 타 언어 작업 지시

대상: **.NET, Node, Java, Kotlin**의 DeliveryDispatch 샘플(+ 있으면 e2e 픽스처).
C++는 이 문서대로 이미 반영·검증 완료(레퍼런스 구현). 스펙이 정본이고, C++는 그 스펙을 구현한
예시일 뿐이다.

## 진행 상태 (2026-07-14)

| 대상 | 상태 |
| --- | --- |
| C++ 샘플 | **완료**(레퍼런스) |
| Node 샘플 | **완료** — `DeliveryOfferStore` + sweeper + `attempt` |
| .NET 샘플 | **완료·실측 그린** — `run_sample.sh` exit 0. 로그 evidence: `offer expired attempt=1` → `decision attempt=2` (courier-b 수락) |
| Java 샘플 | **완료·실측 그린** — `delivery-reassign` 문자열로 재배차를 **흉내내던** 분기를 제거하고 실제 시한 만료로 재배차하게 했다 |
| Kotlin 샘플 | **완료·실측 그린** — Java와 동일. `ZLinkSuspendingSpotPacketHandler`는 `ZLinkSpot` 바운드라 entry spot에 쓸 수 없어 `ZLinkSpotPacketHandler`를 직접 구현했다 |
| 테스트 가드 | **완료** — java testkit 계약 이름 갱신, .NET 회귀 테스트가 이제 `SendToSpot`·`DeliveryOfferStore`·`OfferDeadlineSweeper`를 요구하고 `TaskCompletionSource` 재등장을 실패로 잡는다 |
| **C++ e2e 픽스처** | **미착수** — `framework/languages/cpp/e2e/DeliveryDispatch`는 아직 `offer_delivery_req_t`/`offer_delivery_res_t` request/reply다. 샘플과 별개 픽스처이므로 남은 작업으로 분리한다 |

**언어별 함정.**

- **Java/Kotlin: dispatch가 채널 server가 되어야 한다.** 결정이 one-way로 돌아오려면 courier node가
  보낼 상대가 있어야 하는데, 기존에는 dispatch 채널 자체가 없었다(HTTP가 in-process 큐에 바로 넣었다).
  `deliverydispatch.dispatch` 채널을 새로 만들고 러너의 포트 예약도 하나 늘렸다.
- **Kotlin의 첫 결정이 시한을 넘겼다.** 채널 client가 처음 붙는 비용 때문에 900ms 시한을 놓쳐
  "늦게 온 결정"으로 버려졌다. 시한을 1500ms로 올려 해소했다(시한 값은 언어별 상수이며 cpp 700 /
  java 900과 이미 다르다).
- **재배차 경로에도 `PickedUp`이 생긴다.** 수락 처리를 한 곳으로 모았으므로 evidence 기대 순서가
  `Assigned → Reassigned → Accepted → PickedUp → Delivered`가 된다. cpp가 이미 그렇다.

---

## 1. 무엇이 문제였나

현재 배송 제안(offer)이 **request/reply**로 되어 있다.

```
DispatchWorker --(OfferDeliveryReq, 응답 대기)--> CourierActorNode --> CourierActor
                                                       ↑ 여기서 배송원(사람)의
                                                         결정이 올 때까지 대기
```

노드는 배송원 세션에 알림을 보내고, **사람이 수락/거절을 누를 때까지** 그 요청을 붙잡고 있다.
즉 사람의 반응 시간이 RPC 응답 시간에 묶여 있다. 언어별 현재 구현:

| 언어 | 대기 방식 | 성격 |
| --- | --- | --- |
| C++ | `std::condition_variable` 랑데부 | 스레드 blocking |
| Java / Kotlin | `CompletableFuture.get(timeout)` | 스레드 blocking |
| .NET / Node | `await` (TaskCompletionSource / Promise) | 스레드는 안 막지만 **핸들러 턴이 안 끝난다** |

blocking이 아니어도 문제는 그대로다. 결정을 기다리는 동안 그 핸들러 턴이 살아 있고, actor
mailbox(또는 spot 직렬 큐)의 다음 job이 그만큼 밀린다. 사람의 결정 시간(수 초~수 분)을 런타임
자원에 묶는 설계 자체가 틀렸다.

## 2. 어떻게 바꾸나

**request/reply를 send/send 쌍으로 쪼갠다.** 기다리는 쪽을 없애고, 진행 상태를 기록해서
다음 단계를 정한다.

```
1. DispatchWorker: 제안 상태 기록(Attempt=1, Deadline) + Assigned 이벤트 + OfferDeliveryMsg one-way send → 턴 종료
2. CourierActorNode: 받은 제안을 actor에게 넘기고 즉시 리턴(응답 없음)
3. CourierActor:     배송원 세션에 알림. 여기서 아무도 기다리지 않는다
4. 배송원 결정 도착 → CourierActor가 OfferDeliveryResultMsg를 배차 채널로 one-way send
5. DispatchWorker: Attempt가 현재 제안과 같으면 처리(수락→Accepted/PickedUp/Delivered, 거절→다음 후보 재제안).
                   다르면 늦게 온 결정이므로 버린다
6. Sweeper(주기 타이머): 시한 지난 제안 → Reassigned + 다음 후보에게 재제안. 후보가 다 떨어지면 Failed
```

**정본은 공통 sample spec이다: `framework/doc/framework/common/sample/deliverydispatch/README.ko.md`**
- §7.2 계약표: `OfferDeliveryMsg` / `OfferDeliveryResultMsg` (둘 다 응답 없는 one-way send)
- §7.4 "배송 제안은 왜 request/reply가 아닌가 (구현 지침)": 상태 레코드 필드, 5단계 루프,
  지켜야 할 규칙이 그대로 적혀 있다. **이 절을 먼저 읽고 시작할 것.**

## 3. 계약 변경 (모든 언어 공통)

삭제: `OfferDeliveryReq` / `OfferDeliveryRes`

추가:

| 메시지 | 방향 | 필드 |
| --- | --- | --- |
| `OfferDeliveryMsg` | DispatchWorker → 대상 SpotNode → CourierActor | DeliveryId, CourierId, **Attempt**, PickupAddress, DropoffAddress |
| `OfferDeliveryResultMsg` | CourierActor → DispatchWorker | DeliveryId, CourierId, **Attempt**, Accepted, Reason |

`Attempt`가 늦게 도착한 결정을 걸러내는 유일한 장치다. 빠뜨리지 말 것.

## 4. 컴포넌트별 할 일

**DispatchWorker (배차 서비스)**
- 제안 상태 저장소: `DeliveryOffer { DeliveryId, CourierId, Attempt, Deadline, Status: Offered|Accepted|Rejected|Expired }`.
  샘플은 in-memory여도 되지만, 스펙이 말하는 정본은 "기록"이다 — Redis 같은 store를 쓰는 것이 원칙에 맞다.
- `AssignDeliveryMsg` 핸들러: 제안 기록(Attempt=1) → `Assigned` 상태 발행 → `OfferDeliveryMsg` one-way send → **끝**. 대기 금지.
- `OfferDeliveryResultMsg` 핸들러(신규): Attempt 대조 → 수락이면 Accepted→PickedUp→Delivered 발행,
  거절이면 다음 후보로 재제안(`Reassigned` 발행 + Attempt+1로 다시 send).
- **Sweeper**: 주기적으로 시한 지난 제안을 훑어 재제안, 후보 소진 시 `Failed`.
  제안 시한은 **DispatchWorker가 소유한다**. 노드도 세션도 시한을 세지 않는다.

**CourierActorNode**
- entry spot의 offer 핸들러는 **응답 없는 relay**로 바꾼다. 랑데부(`condition_variable` /
  `CompletableFuture.get` / 결정 대기 `await`)를 **전부 제거**한다.
- actor는 진행 중인 제안의 Attempt를 들고 있다가(`Map<DeliveryId, Attempt>`), 배송원 결정이 오면
  그 Attempt를 실어 `OfferDeliveryResultMsg`를 배차 채널로 one-way send 한다.
- 노드가 배차 채널의 **client**로 참여해야 한다(결정 반환 경로). 서버로 열지는 않는다.

**CourierSession**
- 이미 `CourierDecisionMsg`를 actor로 보내고 있으면 그대로 둔다. 결정 전달 경로만 확인.

## 5. 지켜야 할 것 (스펙 §7.4)

- **어떤 핸들러도 배송원의 결정을 기다리지 않는다.** blocking wait(`condition_variable`,
  `Future.get`)도, 결정 완료를 기다리는 `await`도 금지다.
- 제안 시한은 DispatchWorker의 것이다. 노드/세션은 타이머를 두지 않는다.
- `Attempt`로 늦게 온 결정을 버린다.
- 상태 레코드가 재개 지점이다 — 프로세스가 죽었다 살아나도 기록을 읽고 이어간다.

## 6. 검증

- 샘플 러너 그린: 성공 배송(Assigned→Accepted→PickedUp→Delivered)과 재배차
  (Assigned→**Reassigned**→Accepted→PickedUp→Delivered) 두 시나리오 모두.
- 언어별 sample parity / layout contract 테스트가 `OfferDeliveryReq`를 강제하고 있으면 새 계약으로 갱신한다.
  C++ 쪽은 `test_cpp_framework_sample_parity.cpp`에서
  `OfferDeliveryMsg`/`OfferDeliveryResultMsg` + `class dispatch_state_t` + `class offer_deadline_sweeper_t`를
  요구하고, CourierActorNode에 `condition_variable`이 남아 있으면 실패하도록 바꿨다. 같은 성격의 가드를 권장.
- e2e 픽스처(`framework/languages/<lang>/e2e/DeliveryDispatch`)에도 같은 계약이 있으면 함께 옮긴다.

## 7. C++ 레퍼런스 (참고용, 그대로 베끼지 말고 언어 관용에 맞출 것)

- `framework/languages/cpp/samples/DeliveryDispatch/Shared/Contracts/messages.hpp` — 새 계약
- `framework/languages/cpp/samples/DeliveryDispatch/Server/Dispatch/main.cpp` —
  `dispatch_state_t`(제안 기록), `dispatch_worker_t`(start/settle/reassign), `offer_deadline_sweeper_t`
- `framework/languages/cpp/samples/DeliveryDispatch/Server/CourierActorNode/main.cpp` —
  one-way relay + actor의 `offered_attempts` + 결정 반환

[스펙 목차](../README.ko.md)

# Draft -- Peer Admission

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 상수를
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 합친다.

## 1. 목적

이 초안은 ROUTER 소켓과 `SpotNode`가 점검, 롤링 재시작, 점진적 교체 같은 운영
상황에서 "새 요청은 더 이상 받지 않되, 이미 들어온 작업은 잠시 더 처리"하는
동작을 단순한 상태 모델로 표현하는 방향을 정리한다.

이 초안이 다루는 핵심 목표는 아래와 같다.

- 내려갈 예정인 주체를 peer가 새 작업 대상으로 고르지 않게 한다.
- 연결을 바로 끊지 않고, 사용자가 충분한 시간을 두고 종료를 결정할 수 있게 한다.
- Discovery를 쓰는 경우와 raw peer를 직접 연결하는 경우를 같은 개념으로 설명할
  수 있게 한다.

## 2. 범위

이 초안은 **서버 역할을 하는 routed 수신 주체의 요청 수락 상태**를 다룬다.

- 직접 대상:
  - raw `ROUTER`
  - `SpotNode`
- 영향 받는 쪽:
  - 그 주체에 연결된 peer의 outbound 판단
- 직접 바꾸지 않는 것:
  - draining 상태가 된 주체 자신의 recv, send, request, reply 동작

즉 이 기능은 "로컬 주체가 어떤 API를 쓸 수 있는가"를 바꾸는 기능이 아니라,
"peer가 이 주체를 새 작업 대상으로 써도 되는가"를 알려 주는 기능이다.

다만 구현 경로는 대상마다 다를 수 있다.

- `SpotNode`는 이미 peer control 성격의 내부 제어 경로가 있으므로, 상태 전파와
  peer cache 갱신을 비교적 자연스럽게 붙일 수 있다.
- raw `ROUTER` / `DEALER`는 현재 peer admission 상태를 주고받는 별도 제어면이
  없으므로, 같은 동작을 넣으려면 raw peer protocol 또는 그에 준하는 상태 전파
  경로가 추가로 필요하다.

이 차이는 구현 난이도의 차이일 뿐, 이 초안이 목표로 하는 최종 모델 자체를
나누지는 않는다. 즉 정식화 단계에서는 `SpotNode`와 raw `ROUTER` / `DEALER`
모두가 같은 admission 개념을 공유하는 방향을 전제로 한다.

## 3. 상태 모델

이 초안은 transport 연결 상태와 구분하기 위해 아래 두 상태를 가정한다.

| 상태 | 뜻 |
|------|----|
| `SERVING` | peer가 이 주체를 새 작업 대상으로 사용할 수 있다 |
| `DRAINING` | peer가 이 주체를 새 작업 대상으로 사용하면 안 된다 |

이 이름은 초안 단계의 제안이다. 공개 상수 이름은 구현 시점에 다시 확정한다.

이 상태는 socket monitor의 connected/disconnected 같은 transport 상태와 같은
뜻이 아니다. 주체가 `DRAINING`이더라도 실제 연결은 살아 있을 수 있다.

## 4. 기본 의미

### 4.1 SERVING

- peer는 이 주체로 새 application outbound를 보낼 수 있다.
- Discovery를 쓰는 경우에는 outbound 후보 목록에 포함될 수 있다.

### 4.2 DRAINING

- peer는 이 주체를 새 application outbound 대상에서 제외해야 한다.
- 다만 draining 상태의 주체 자신은 평소처럼 들어온 메시지를 처리할 수 있다.
- 사용자는 이 상태로 바꾼 뒤 충분히 기다리고, 그 다음 재시작 또는 종료를
  결정할 수 있다.

## 4.3 공개 C API 초안

이 초안은 요청 수락 상태를 바꾸는 공개 C API가 필요하다고 본다.
이 기능은 단순 init-only 옵션이 아니라 runtime control path 성격이 강하므로,
의미가 바로 드러나는 전용 함수를 두는 방향을 우선 제안한다.

```c
typedef enum zlink_admission_state_t
{
    ZLINK_ADMISSION_SERVING = 1,
    ZLINK_ADMISSION_DRAINING = 2
} zlink_admission_state_t;

zlink_config_result_t zlink_set_admission_state (
  void *handle_,
  zlink_admission_state_t state_);

zlink_config_result_t zlink_get_admission_state (
  void *handle_,
  zlink_admission_state_t *state_out_);
```

의도는 단순하다.

- `set`은 이 handle이 peer에게 어떤 admission 상태로 보일지를 바꾼다.
- `get`은 현재 로컬 handle의 admission 상태를 조회한다.

초안 단계의 기대 동작은 아래와 같다.

- 기본값은 `SERVING`
- `SERVING -> DRAINING` 전환은 runtime에 허용
- `DRAINING -> SERVING` 복귀도 runtime에 허용
- 잘못된 상태 값은 설정 실패
- 지원 대상이 아닌 handle에 호출하면 실패

현재 초안 기준의 지원 대상은 아래 둘을 우선 가정한다.

- raw `ROUTER`
- `SpotNode`

반환 타입은 control/configuration 계열 API와 맞추기 위해
`zlink_config_result_t`를 가정한다. 공개 상수 이름과 정확한 에러 계약은 구현 시
확정한다.

## 4.4 draining peer submit 실패 결과 초안

이 초안은 draining peer를 향한 outbound submit 실패를 기존 결과 코드에 억지로
넣기보다, 별도 public submit 결과로 표현하는 방향을 제안한다.

```c
/* Draft name only */
ZLINK_SUBMIT_NOT_ADMITTED
```

초안 의미는 아래와 같다.

- submit 시점에 local peer가 알고 있는 remote admission 상태가 `DRAINING`이다.
- 그 상태 판단 때문에 새 application outbound를 허용하지 않는다.

이 의미는 기존 공개 결과와 다르다.

- `NOT_CONNECTED`와 다름:
  연결이 없는 것이 아니라 연결은 살아 있다.
- `NOT_FOUND`와 다름:
  대상을 찾지 못한 것이 아니라 대상 상태 때문에 거부된 것이다.
- `INVALID_STATE`와 다름:
  로컬 handle 상태 오류가 아니라 remote peer admission 거부다.

다만 이 초안은 상태 전파를 최선 노력의 runtime 신호로 본다. 따라서 실제 경합
상황에서는 아래 같은 기존 실패가 먼저 관찰될 수도 있다.

- 상태 캐시를 보기 전에 연결이 먼저 사라져 `NOT_CONNECTED`가 관찰되는 경우
- 대상 조회 자체가 먼저 실패해 `NOT_FOUND`가 관찰되는 경우

즉 `ZLINK_SUBMIT_NOT_ADMITTED`는 "submit 시점에 local이 remote peer를
`DRAINING`으로 알고 있어 admission 이유로 거부했다"는 의미로 해석하는 편이
맞다.

따라서 현재 초안은 아래 submit 실패를 모두 같은 bucket으로 묶는 방향을
제안한다.

- DEALER가 `DRAINING` ROUTER만 알고 있을 때의 outbound submit 실패
- ROUTER가 `DRAINING` 상태의 target RID로 `zlink_send_rid()`를 시도했을 때의 실패
- ROUTER가 `DRAINING` 상태의 target RID로 `zlink_router_request()`를 시도했을 때의 실패
- Spot direct request가 `DRAINING` 상태의 대상에 의해 거부될 때의 실패

내부 errno 매핑은 아직 확정하지 않는다. 다만 현재 초안은 의미상
`ECONNREFUSED` 계열이 가장 가깝다고 본다.

## 5. Peer 쪽 영향

이 초안에서 상태를 실제로 강제하는 쪽은 **peer**다.

### 5.1 DEALER가 보는 ROUTER

DEALER가 연결한 ROUTER 집합 중 어떤 ROUTER가 `DRAINING` 상태가 되면,
DEALER는 그 ROUTER를 outbound 후보에서 제외해야 한다.

이 초안은 이를 단순 구현 힌트가 아니라 **peer 쪽 계약**으로 본다.
즉 `DEALER -> ROUTER` 관계에서 remote ROUTER가 `DRAINING` 상태이면,
DEALER는 그 ROUTER로 새 application outbound를 보내면 안 된다.

이 규칙은 아래 둘 모두에 적용하는 방향을 가정한다.

- `zlink_send()` 같은 DEALER outbound send
- `zlink_dealer_request()` 같은 DEALER outbound request

쉽게 말하면, DEALER 내부의 round-robin 후보 목록에서 draining ROUTER를 빼는
모델이다.

계약 관점에서 쓰면 아래와 같다.

- DEALER는 `DRAINING` 상태의 ROUTER를 send 대상 후보에 넣으면 안 된다.
- DEALER는 `DRAINING` 상태의 ROUTER를 request 대상 후보에 넣으면 안 된다.
- 여러 ROUTER 중 일부만 `DRAINING`이면 `SERVING` 상태의 ROUTER만 사용해야 한다.
- 현재 알고 있는 ROUTER가 모두 `DRAINING`이면 submit은 실패해야 한다.

이 실패는 현재 초안에서는 `ZLINK_SUBMIT_NOT_ADMITTED`로 표현하는 방향을
제안한다.

### 5.2 ROUTER가 보는 다른 ROUTER

ROUTER가 다른 ROUTER에게 `routing_id`를 지정해서 보내는 경우에는 대상 peer의
상태를 보고 판단해야 한다.

- 대상 RID가 `SERVING`이면 평소처럼 submit 가능
- 대상 RID가 `DRAINING`이면 즉시 실패

이 규칙은 아래 둘에 적용하는 방향을 가정한다.

- `zlink_send_rid()`
- `zlink_router_request()`

즉 ROUTER 쪽에서는 "지금 이 RID가 새 작업을 받아도 되는 peer인가"를 먼저 보고,
그렇지 않으면 submit 단계에서 바로 실패시키는 모델이다.

이 실패는 현재 초안에서는 `ZLINK_SUBMIT_NOT_ADMITTED`로 표현하는 방향을
제안한다.

### 5.3 SpotNode를 보는 peer

Spot direct request를 보내는 peer도 같은 개념을 따라야 한다.

- 대상 `SpotNode`가 `SERVING`이면 평소처럼 submit 가능
- 대상 `SpotNode`가 `DRAINING`이면 새 outbound는 즉시 실패 또는 후보 제외

이 초안은 raw ROUTER와 `SpotNode`가 같은 admission 의미를 공유하는 방향을
가정한다. 다만 Spot direct request API에 어떤 결과 코드와 조회 경로를 연결할지는
구현 전에 더 정해야 한다.

현재 초안은 Spot direct request 쪽도 같은 공개 결과
`ZLINK_SUBMIT_NOT_ADMITTED`를 재사용하는 방향을 우선 제안한다.

초안 단계에서 admission 적용 후보로 보는 공개 submit path는 아래 정도다.

- `SpotNode`를 직접 대상으로 삼는 request 계열 submit API
- `SpotNode` peer 목록을 후보 집합으로 삼아 대상을 고르는 submit API

반면 정확히 어느 공개 함수가 이 규칙의 직접 대상이 되는지는 구현 시점에
정식 spec에서 확정한다.

## 6. 로컬 주체의 동작

draining 상태가 된 주체 자신의 동작은 이 초안에서 바꾸지 않는다.

- recv 가능
- handler dispatch 가능
- send 가능
- request 가능
- reply 가능

이 초안의 의도는 로컬 주체를 강제로 멈추는 것이 아니다.
peer가 그 주체를 새 작업 대상으로 사용하지 않게 해서, 사용자가 충분한 대기
시간을 확보할 수 있게 하는 것이다.

즉 draining 상태는 "내가 지금 아무 것도 못 한다"가 아니라
"남이 나를 새 작업 대상으로 고르면 안 된다"는 뜻이다.

## 7. 전파 방식

이 상태 정보는 연결된 peer에게 공유되어야 한다.

이 초안은 아래 방향을 가정한다.

- `ROUTER`와 `SpotNode`는 자신의 admission 상태가 바뀌면 연결된 peer들에게 그
  변경을 알려야 한다.
- peer는 자신이 알고 있는 remote 주체의 상태 캐시를 갱신한다.
- 재연결 후에는 현재 상태를 다시 동기화해야 한다.

구체적인 wire 형식은 이 초안에서 확정하지 않는다.
다만 요구되는 관찰 가능한 동작은 단순하다.

- 상태가 `SERVING -> DRAINING`으로 바뀌면 peer outbound 판단이 바뀌어야 한다.
- 상태가 `DRAINING -> SERVING`으로 바뀌면 peer가 다시 그 주체를 outbound
  후보로 사용할 수 있어야 한다.

상태 전파는 초안 단계에서 **최선 노력의 runtime 제어 신호**로 본다.
즉 상태 변경 직후 모든 peer가 같은 시점에 동시에 바뀐 상태를 보장하는 강한
동기 모델은 이 초안의 목표가 아니다.

또한 구현 경로는 대상별로 달라질 수 있다.

- `SpotNode`는 기존 peer control 경로를 확장하는 방향이 자연스럽다.
- raw `ROUTER` / `DEALER`는 별도 제어 frame, metadata 교환, 또는 monitor와
  연계된 상태 동기화처럼 새로운 raw peer 상태 전파 경로가 필요할 수 있다.

현재 초안에서 열어 두는 전파 방식 후보는 아래 정도다.

- raw peer protocol에 admission 상태 전용 제어 frame을 추가
- 연결 직후 또는 재연결 직후 metadata 교환으로 현재 상태를 동기화
- Discovery / service projection을 통해 관찰 가능한 상태를 별도로 노출

이 문서는 셋 중 어느 방식을 채택할지 아직 확정하지 않는다. 다만 최종적으로는
peer submit 판단과 monitor 관찰이 같은 상태 의미를 보게 해야 한다.

## 8. Discovery와의 관계

Discovery는 이 상태의 **원본**이라기보다, 상태를 관찰하고 노출하는 상위 제어면이
될 수 있다.

이 초안은 아래 정도를 기대한다.

- Discovery-managed peer 목록에서도 ROUTER와 `SpotNode`의 요청 수락 상태를 볼
  수 있다.
- Discovery를 쓰는 DEALER는 그 상태를 이용해 outbound 후보를 정리할 수 있다.
- 사용자가 peer 목록을 직접 보는 경우에도 각 peer가 `SERVING`인지
  `DRAINING`인지 확인할 수 있다.

상태 조회 API의 모양은 아직 열려 있다.
가능한 방향은 둘 중 하나다.

- 기존 peer 목록 조회에 상태 필드를 추가
- 상태가 반영된 별도 helper 조회를 추가

이 초안은 **상태가 보인다**는 요구사항만 적고, 공개 API 모양은 구현 시점에
결정한다.

## 9. 문서 반영 범위

이 변경이 구현되면 아래 문서들이 함께 갱신되어야 한다.

- `core/include/zlink.h`
- `core/include/zlink_errno.h`
- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`
- `doc/spec/core/socket/dealer.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/service/discovery.md`
- `doc/spec/core/service/registry.ko.md`
- `doc/spec/core/errno-map.ko.md`
- `doc/spec/core/errno-map.md`
- 관련 bindings 문서

특히 `ZLINK_SUBMIT_NOT_ADMITTED`를 추가한다면 공개 enum, errno 매핑, 바인딩
반환값 문서가 함께 바뀌어야 한다.

## 10. 모니터링과 상태 변화 관찰

이 초안은 peer 쪽이 remote admission 상태 변화를 **모니터 이벤트로도**
관찰할 수 있어야 한다고 본다.

이 요구가 필요한 이유는 단순하다.

- peer가 상태 캐시를 내부에서만 조용히 바꾸면 응용은 왜 outbound 후보가
  바뀌었는지 알기 어렵다.
- 운영 도구와 테스트는 상태 전이를 이벤트로 관찰할 수 있어야 문제를 추적하기
  쉽다.

초안 단계의 요구 동작은 아래와 같다.

- remote peer가 `SERVING -> DRAINING`으로 바뀌면 연결된 peer 쪽 모니터가 그
  변화를 감지할 수 있어야 한다.
- remote peer가 `DRAINING -> SERVING`으로 돌아오면 역시 모니터로 감지할 수
  있어야 한다.
- 이벤트 payload에는 적어도 **어느 peer의 상태가 바뀌었는지**를 식별할 수 있는
  정보가 있어야 한다.

이 요구는 아래 모든 관찰 주체에 적용하는 방향을 가정한다.

- raw `DEALER`
- raw `ROUTER`
- `SpotNode`

가능한 방향은 둘이다.

- raw socket monitor에 전용 이벤트를 추가
- service monitor 경로에서 provider 상태 변화로 노출

raw peer protocol 자체가 상태를 전파한다는 이 초안의 전제와 더 직접 맞는 방향은
첫 번째다. 따라서 현재 초안은 아래와 같은 raw socket monitor 이벤트와 service
monitor 이벤트가 추가될 가능성을 열어 둔다.

```c
/* Draft names only */
ZLINK_EVENT_PEER_ADMISSION_CHANGED
ZLINK_SERVICE_EVENT_PEER_ADMISSION_CHANGED
```

이 이벤트들의 초안 의미는 아래와 같다.

- remote peer의 admission 상태가 바뀌었음을 알린다.
- payload에는 상태가 바뀐 peer를 식별할 수 있는 정보가 들어 있어야 한다.
- `value` 필드는 새 상태(`SERVING` 또는 `DRAINING`)를 나타내는 데 쓸 수 있다.

raw `ROUTER`와 raw `DEALER`에서는 `routing_id` 하나로 peer를 식별하는 방향이
자연스럽다.

하지만 `SpotNode` monitor 쪽은 어느 identity를 canonical peer 식별자로 둘지
아직 더 정해야 한다. 예를 들어 아래 후보가 있을 수 있다.

- node `routing_id`
- spot `routing_id`
- 둘을 함께 담는 복합 식별 정보

따라서 이 초안은 `SpotNode` monitor 이벤트가 "peer를 식별할 수 있어야 한다"는
요구만 먼저 기록하고, 정확한 식별자 모양은 정식 spec 단계에서 확정한다.

관찰 경로의 초안 방향은 아래처럼 나눈다.

- raw `DEALER`, raw `ROUTER`
  socket monitor에서 peer admission 변화 이벤트를 받을 수 있어야 한다.
- `SpotNode`
  service monitor 또는 그에 준하는 공개 monitor surface에서 peer admission
  변화 이벤트를 받을 수 있어야 한다.

이 문서는 정확한 상수 이름, `value` 인코딩, socket monitor와 service monitor 중
어느 쪽을 canonical 경로로 둘지는 아직 확정하지 않는다. 다만 **상태 변화가
모니터로 관찰 가능해야 한다**는 요구만 먼저 기록한다.

## 11. 운영 시나리오

이 초안이 염두에 두는 대표 시나리오는 아래와 같다.

1. 서버 쪽 `ROUTER` 또는 `SpotNode`가 `SERVING` 상태로 운영 중이다.
2. 사용자가 점검 전에 그 주체를 `DRAINING`으로 바꾼다.
3. peer는 그 주체를 새 outbound 대상으로 쓰지 않는다.
4. 로컬 주체는 이미 들어온 작업을 계속 처리한다.
5. 사용자는 충분히 기다린 뒤 재시작 또는 종료를 수행한다.
6. 서버가 다시 살아나면 상태를 `SERVING`으로 되돌린다.
7. peer는 다시 그 주체를 outbound 후보로 사용할 수 있다.

서버가 완전히 사라진 경우의 disconnect, peer 제거, 재연결 관리는 Discovery와
기존 transport/liveness 경로가 담당한다.

## 12. 비목표

이 초안은 아래를 보장하지 않는다.

- 장애 상황에서의 정확히 한 번 처리
- timeout 뒤 retry의 중복 실행 방지
- application payload 의미를 해석한 send 차단 정책
- 사용자가 충분히 기다리지 않고 종료했을 때의 완료 보장
- Registry가 원격으로 직접 admission 상태를 바꾸는 운영 제어 기능

즉 이 초안은 graceful maintenance를 돕는 admission 신호를 다루는 것이지,
분산 트랜잭션이나 exactly-once 실행 의미를 제공하는 문서는 아니다.

운영 목적의 원격 제어가 필요하면, 사용자가 별도 운영 경로를 구현해서 로컬
process의 admission 상태 변경 API를 호출하는 방향을 전제한다.

## 13. 미결 사항

구현 전에 아래 사항은 더 확정해야 한다.

- 공개 상태 이름을 `SERVING/DRAINING`으로 할지, 다른 이름을 쓸지
- 상태 변경을 전용 `set/get` 함수로 둘지, option으로 녹일지
- `ZLINK_SUBMIT_NOT_ADMITTED` 같은 새 submit 결과를 실제로 추가할지
- draining peer submit 실패의 내부 errno를 무엇으로 둘지
- 상태 조회를 기존 peer 목록 확장으로 할지, 별도 helper로 할지
- raw peer protocol과 Discovery projection이 같은 상태를 어떻게 공유할지

## 14. 구현 순서 메모

이 절은 구현 전 초안의 **비규범 작업 메모**다.

세 초안 전체를 함께 본다면, 이 변경은 구현 순서상 **3순위**로 보는 편이
자연스럽다.

- 공개 API, submit 결과, peer 상태 전파, monitor, Discovery 노출이 함께
  움직인다.
- `SpotNode`와 raw `ROUTER` / `DEALER` 모두 같은 개념으로 맞춰야 한다.
- 세 초안 중 가장 범위가 넓고 교차 영향이 많다.

## 15. 회귀 테스트 포인트

이 절은 구현 전 초안의 **비규범 검증 메모**다. 공개 계약을 새로 정의하지는
않고, 구현 후 어떤 관찰 항목을 회귀 테스트로 확인해야 하는지 정리한다.

- `ROUTER`가 `SERVING -> DRAINING`으로 바뀌면 연결된 `DEALER`가 그 peer를
  round-robin 후보에서 제외하는지 확인한다.
- 여러 `ROUTER` 중 일부만 `DRAINING`이면 `DEALER` outbound가 계속 `SERVING`
  peer로만 가는지 확인한다.
- `DEALER`가 알고 있는 `ROUTER`가 모두 `DRAINING`이면 `zlink_send()`와
  `zlink_dealer_request()`가 `ZLINK_SUBMIT_NOT_ADMITTED`로 실패하는지
  확인한다.
- `ROUTER -> ROUTER` 경로에서 target `routing_id`가 `DRAINING`이면
  `zlink_send_rid()`와 `zlink_router_request()`가
  `ZLINK_SUBMIT_NOT_ADMITTED`로 실패하는지 확인한다.
- `ROUTER` 또는 `SpotNode`가 `DRAINING -> SERVING`으로 돌아오면 peer 쪽
  outbound 후보에 다시 포함되는지 확인한다.
- remote peer admission 상태가 바뀌면 raw `DEALER`, raw `ROUTER`,
  `SpotNode` monitor에서 상태 변화 이벤트를 관찰할 수 있는지 확인한다.
- peer 재연결 후에도 최신 admission 상태가 다시 동기화되어, 이전 캐시 때문에
  잘못된 outbound 허용 또는 차단이 남지 않는지 확인한다.

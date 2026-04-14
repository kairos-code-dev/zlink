[스펙 목차](../README.ko.md)

# Draft -- Peer Auto Connect

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 합친다.

## 1. 목적

이 초안은 Discovery를 통한 자동 연결에서 peer 쌍마다 누가 연결을 시작할지
라이브러리 내부에서 결정하는 방향을 정리한다.

이 초안이 다루는 핵심 목표는 아래와 같다.

- 사용자가 "누가 누구에게 connect 해야 하는가"를 따로 설계하지 않아도 되게
  한다.
- 같은 peer 쌍이 서로 동시에 dial해서 중복 연결 경쟁을 만드는 일을 줄인다.
- `ROUTER <-> ROUTER`처럼 양쪽이 모두 outbound를 시작할 수 있는 경우에도
  pair마다 한쪽만 dial하도록 만든다.

## 2. 배경 문제

자동 연결은 사용성이 좋아야 한다. 사용자가 서비스 이름만 맞추고 Discovery에
붙였는데, 추가로 "이 경우에는 A가 B로 connect하고 저 경우에는 B가 A로
connect하라"는 규칙까지 고민해야 하면 사용성이 크게 떨어진다.

하지만 `ROUTER <-> ROUTER` 관계는 단순하지 않다.

- 한 번의 connect만으로 연결은 이미 양방향이다.
- 그런데 양쪽이 서로 dial하면 "양방향 강화"가 아니라 중복 연결 경쟁에
  가까워진다.
- duplicate peer는 handover 또는 무시 정책으로 정리될 수 있지만, 그 정책이
  누가 initiator여야 하는지까지 대신 결정해 주지는 못한다.

즉 이 초안은 **사용자에게 규칙을 넘기지 않으면서도**, 라이브러리 내부에서는
pair마다 하나의 initiator만 남게 만드는 정책이 필요하다고 본다.

## 3. 범위

이 초안은 **peer 발견 뒤 connect 시도를 만들 것인가 말 것인가**를 다룬다.

- 직접 대상:
  - Discovery-managed auto connect
  - 같은 서비스 안의 `ROUTER <-> ROUTER` 관계
- 간접 영향:
  - duplicate dial 감소
  - handover churn 감소
  - 재연결 시 방향 결정의 일관성
- 직접 다루지 않는 것:
  - admission state
  - request/reply 의미
  - retry, timeout, exactly-once 같은 상위 전송 의미

즉 이 문서는 "새 작업을 보내도 되는가"가 아니라, "이 peer 쌍에서 누가
connect를 시작하는가"를 다룬다.

## 4. 기본 관찰

### 4.1 한 번의 connect면 양방향이다

한 peer가 다른 peer에게 connect해서 세션이 성립하면, 그 연결은 이미 양방향
메시지 경로를 제공한다.

따라서 `A -> B` connect가 이미 성립했다면, 그 사실만으로 아래가 가능해야 한다.

- A가 B에게 보냄
- B가 A에게 보냄

즉 `A -> B`와 `B -> A`를 둘 다 만들 필요가 기본적으로 없다.

### 4.2 상호 dial은 중복 연결 경쟁을 만든다

같은 peer 쌍이 서로 dial하면 아래 같은 문제가 생길 수 있다.

- 연결 수가 필요 이상으로 늘어난다.
- duplicate peer가 생기고, 어느 쪽 pipe가 남을지 timing에 따라 달라질 수 있다.
- handover가 켜져 있으면 기존 pipe와 새 pipe가 서로 교체되며 churn이 생길 수
  있다.
- 재연결 상황에서 양쪽이 다시 동시에 dial하면서 같은 경쟁이 반복될 수 있다.

따라서 이 초안은 `ROUTER <-> ROUTER` 자동 연결을 **쌍마다 한쪽만 dial**하는
정책으로 보는 것이 맞다고 본다.

## 5. 정책 방향

### 5.1 사용자 규칙이 아니라 라이브러리 내부 규칙

이 초안은 사용자가 별도 connect 규칙을 설정하는 방향을 권하지 않는다.

사용자 경험은 아래처럼 단순해야 한다.

- 같은 서비스의 peer를 Discovery에 붙인다.
- 라이브러리가 필요한 연결 관계를 자동으로 만든다.

대신 실제 dial 여부는 Discovery 또는 그와 같은 control path가 **내부 규칙**으로
판단한다.

### 5.2 pairwise initiator rule

같은 peer 쌍 `A`, `B`가 서로를 발견했을 때, 양쪽이 모두 dial을 시도하지 않고
오직 한쪽만 dial을 시작해야 한다.

이 초안은 이를 `pairwise initiator rule`이라고 부른다.

이 규칙의 요구 조건은 아래와 같다.

- 두 peer가 같은 입력을 보면 같은 결론에 도달해야 한다.
- 쌍마다 initiator가 정확히 하나만 정해져야 한다.
- 재연결 시에도 같은 비교 기준을 적용할 수 있어야 한다.
- 사용자 설정이 추가로 필요하지 않아야 한다.

## 6. 결정 규칙 초안

### 6.1 정렬 가능한 stable key 필요

pairwise initiator rule을 만들려면 두 peer를 같은 방식으로 정렬할 수 있는 key가
필요하다.

현재 초안은 아래 두 값을 후보로 본다.

- `routing_id`
- advertise endpoint 문자열

### 6.2 우선 비교 기준

현재 초안의 기본 제안은 아래와 같다.

1. `routing_id`가 다르면 `routing_id` 순서로 비교한다.
2. `routing_id`가 같으면 advertise endpoint 문자열 순서로 비교한다.
3. 그래도 같으면 같은 peer 또는 비교 불가로 보고 auto connect를 만들지 않는다.

정렬 방향 자체는 중요하지 않다. 중요한 것은 **모든 peer가 같은 total order를
공유한다**는 점이다.

예를 들어 아래처럼 정의할 수 있다.

- `local_key < remote_key` 이면 local이 dial
- 그렇지 않으면 local은 dial하지 않음

이 규칙이면 `A`, `B` 쌍에 대해 항상 한쪽만 dial한다.

### 6.3 재시작과 방향 변화

`routing_id`가 재시작 후 바뀌는 환경이라면, 다음 실행에서 initiator 방향도
바뀔 수 있다.

이 초안은 그 자체를 오류로 보지 않는다.

중요한 것은 특정 방향을 영구 보장하는 것이 아니라, **각 시점마다 pair당 한쪽만
dial한다**는 점이다.

방향 안정성까지 더 원하면 사용자가 고정 `routing_id`를 주는 방식이 가능할 수
있다. 다만 그것은 자동 연결 정책의 필수 요구는 아니다.

### 6.4 routing_id 충돌과의 관계

이 초안은 `routing_id`를 initiator 비교 key 후보로 보지만, 서로 다른 peer가
같은 `routing_id`를 갖는 상황 자체를 auto connect 정책이 해결한다고 보지는
않는다.

즉 auto connect 정책의 역할은 아래까지다.

- 같은 peer 쌍에서 누가 dial을 시작할지 정한다.
- 상호 dial 경쟁을 줄인다.

반면 아래 문제는 이 정책의 직접 해결 대상이 아니다.

- 서로 다른 peer가 같은 peer identity를 쓰는 경우

이런 충돌이 실제 연결 단계에서 드러나면, duplicate peer 처리 결과는 기존
`ROUTER`의 handover 설정에 따른다.

- handover가 꺼져 있으면 기존 정책에 따라 새 연결이 무시될 수 있다.
- handover가 켜져 있으면 기존 pipe와 새 pipe 사이의 교체가 일어날 수 있다.

따라서 auto connect 정책은 symmetric dial을 줄여 주지만, `routing_id`
충돌 자체를 없애거나 정상화하는 기능은 아니다.

## 7. handover와의 관계

이 초안은 handover를 필요 없는 기능이라고 보지 않는다. duplicate peer가 실제로
생겼을 때 정리하는 정책으로는 여전히 유용하다.

다만 handover는 아래 문제를 해결하는 도구이지, 자동 연결 규칙 그 자체는 아니다.

- 이미 duplicate가 생긴 뒤 어떤 pipe를 남길 것인가

반면 auto connect 정책은 아래를 먼저 해결해야 한다.

- 애초에 누가 dial을 시작할 것인가

즉 이 초안은 아래 두 층을 분리한다.

- `pairwise initiator rule`
  duplicate dial을 만들지 않도록 미리 중재한다.
- handover
  그래도 duplicate가 생긴 예외 상황을 정리한다.

## 8. Discovery에 대한 기대 동작

Discovery를 쓰는 경우, provider 목록이 생겼다고 해서 항상 각 endpoint에 대해
무조건 connect를 시도하면 안 된다.

현재 초안은 아래 동작을 기대한다.

- local peer가 remote peer를 발견한다.
- local과 remote가 `ROUTER <-> ROUTER` 관계인지 확인한다.
- 그렇다면 pairwise initiator rule을 먼저 계산한다.
- local이 initiator인 경우에만 connect 시도를 만든다.
- local이 initiator가 아니면 그 peer에 대한 connect 시도를 만들지 않는다.

즉 Discovery의 역할은 "peer를 전부 보자마자 다 dial"이 아니라, "연결 가능한
peer를 발견한 뒤 이 쌍에서 local이 initiator인지 판단하고 필요한 connect만
만든다"가 된다.

## 9. raw 연결과의 관계

이 초안은 우선 Discovery-managed auto connect를 직접 대상으로 삼는다.

사용자가 raw API만 써서 수동으로 `connect()`를 호출하는 경우까지 라이브러리가
대신 중재하는 것은 이 문서의 직접 범위가 아니다.

즉 raw-only 환경에서는 사용자가 여전히 아래를 직접 책임질 수 있다.

- 서로 dial하지 않도록 구성하기
- duplicate peer를 어떻게 다룰지 판단하기

이 초안이 다루는 것은 **자동 연결 정책**이지, 모든 수동 연결을 강제로 다른
정책으로 바꾸는 기능은 아니다.

## 10. 운영 시나리오

이 초안이 염두에 두는 대표 시나리오는 아래와 같다.

1. 같은 서비스의 `ROUTER` peer `A`와 `B`가 서로를 Discovery에서 본다.
2. 둘 다 같은 비교 규칙으로 pairwise initiator를 계산한다.
3. 예를 들어 `A`만 initiator로 결정되면 `A -> B` connect만 만들어진다.
4. 연결이 성립한 뒤 A와 B는 같은 연결을 통해 양방향 메시지를 주고받는다.
5. 재연결이 필요하면 같은 규칙으로 다시 initiator를 계산한다.
6. duplicate peer가 예외적으로 생기면 handover가 그 상황을 정리한다.

이 모델의 핵심은 **사용자는 서비스만 선언하고, 라이브러리가 pair당 한 번만
dial한다**는 점이다.

## 11. 비목표

이 초안은 아래를 직접 해결하지 않는다.

- request admission 또는 draining 상태
- retry, timeout, exactly-once 보장
- raw 수동 connect 호출의 자동 중재
- handover를 완전히 대체하는 것
- 사용자에게 새로운 connect 규칙 설정 API를 추가하는 것

즉 사용자가 명시적으로 수동 `connect()`를 호출한 경우에는, 그 연결 방향과 중복
의미를 여전히 사용자가 직접 책임질 수 있다.

즉 이 초안은 "자동 연결에서 누가 dial을 시작하는가"만 다루며, 상위 전송 의미나
운영 제어 모델 전체를 한 번에 정의하지 않는다.

## 12. 미결 사항

구현 전에 아래 사항은 더 확정해야 한다.

- 공개 문서에서 이 정책을 어디까지 명시할지
- initiator 비교 key를 `routing_id` 중심으로 둘지, endpoint 중심으로 둘지
- `routing_id` 동률 또는 비가용 상황을 어떤 tie-break로 처리할지
- Discovery projection이 initiator 판단에 필요한 key를 항상 제공하는지
- auto connect 대상에서 `ROUTER <-> ROUTER` 외 다른 조합에도 같은 규칙이
  필요한지
- pairwise initiator 판단 결과를 모니터링이나 진단 정보로 노출할지

## 13. 구현 순서 메모

이 절은 구현 전 초안의 **비규범 작업 메모**다.

세 초안 전체를 함께 본다면, 이 변경은 구현 순서상 **2순위**로 보는 편이
자연스럽다.

- Discovery auto connect 경로가 이미 한 군데에 모여 있다.
- provider snapshot에 필요한 비교 key가 이미 들어 있다.
- 기본값 변경보다 구현 범위는 넓지만, admission 상태 전파보다는 경계가
  명확하다.

## 14. 회귀 테스트 포인트

이 절은 구현 전 초안의 **비규범 검증 메모**다. 공개 계약을 새로 정의하지는
않고, 구현 후 어떤 관찰 항목을 회귀 테스트로 확인해야 하는지 정리한다.

- 같은 서비스의 `ROUTER` 둘이 서로를 발견해도 pair당 실제 outbound connect는
  한 번만 만들어지는지 확인한다.
- `A`, `B`가 각각 서로 다른 시점에 provider 목록을 갱신해도 같은 initiator
  결론에 도달하는지 확인한다.
- 한 번의 connect만으로 양쪽 방향 메시지 교환이 가능한지 확인한다.
- 재연결 상황에서도 같은 비교 규칙으로 initiator가 다시 정해지고, 상호 dial
  경쟁이 반복되지 않는지 확인한다.
- duplicate peer가 예외적으로 생겼을 때 handover가 여전히 마지막 정리 장치로
  동작하는지 확인한다.
- `routing_id` 동률 또는 key 충돌 상황에서 tie-break가 한쪽만 dial하도록
  수렴하는지 확인한다.

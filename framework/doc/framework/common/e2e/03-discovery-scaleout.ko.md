<!-- framework-adapter-nav:start -->
[이전: Channel messaging](02-channel-messaging.ko.md) | [E2E 목차](README.ko.md) | [다음: Publish, subscribe, stream](04-pubsub-stream.ko.md)
<!-- framework-adapter-nav:end -->

# Discovery와 Scale-out E2E

Discovery 기반 자동 연결은 수동 endpoint 테스트보다 실제 운영 구조에 가깝다. 이 문서는
registry topology, provider 증감, scale-out 분산, 같은 routing id의 endpoint 교체,
graceful drain을 검증한다.

## DSC-001 registry topology ready

우선순위: `P0`

구성:

- registry 1개
- provider server 2개
- consumer client 1개

절차:

1. registry를 시작한다.
2. provider server가 channel server 역할을 등록한다.
3. consumer는 Discovery client 역할로 channel을 등록한다.
4. probe가 registry topology를 조회한다.

검증:

- topology에 provider 2개가 `Ready`로 보인다.
- consumer는 endpoint를 직접 알지 않고 request를 보낸다.
- request는 두 provider 중 하나에서 처리된다.

## DSC-002 provider scale-out 추가

우선순위: `P0`

절차:

1. provider A만 시작하고 request를 보낸다.
2. provider B를 추가로 시작한다.
3. topology가 provider 2개를 반영할 때까지 기다린다.
4. 여러 request를 보낸다.

검증:

- B 추가 전에는 A만 호출된다.
- B 추가 후에는 A와 B가 모두 호출된다.
- client 재시작 없이 provider 추가가 반영된다.
- scale-out 직후 첫 요청 일부가 기존 provider로 가더라도, topology 반영 완료 뒤 검증
  구간에서는 새 provider가 routing 대상에 포함된다.

## DSC-003 provider scale-in 제거

우선순위: `P0`

절차:

1. provider A, B를 시작한다.
2. 요청 분산을 확인한다.
3. provider B를 정상 종료한다.
4. topology에서 B가 빠지거나 unavailable 상태가 된다.
5. 다시 request를 보낸다.

검증:

- B 종료 뒤 request는 A로만 간다.
- client는 죽은 endpoint에 계속 요청을 보내 timeout을 반복하지 않는다.
- B 재시작 시 다시 provider 집합에 들어온다.
- `requestToChannel`을 지속적으로 보내는 중 scale-in이 일어나도, 완료된 요청은 정상
  response 또는 정해진 public error로 끝난다. request map에 pending entry가 남지 않는다.

## DSC-004 registry 재시작

우선순위: `P1`

절차:

1. registry, provider, consumer를 시작한다.
2. 정상 request를 확인한다.
3. registry를 재시작한다.
4. provider와 consumer가 registry에 다시 연결되는지 기다린다.

검증:

- registry 재시작 중 기존 연결이 가능한 경우 기존 request는 계속 성공한다.
- topology 복구 뒤 신규 provider discovery가 다시 동작한다.
- 재등록 중복 때문에 provider가 두 번 보이지 않는다.

## DSC-005 provider readiness 지연

우선순위: `P1`

절차:

1. provider 프로세스는 시작하지만 channel server ready를 늦춘다.
2. registry에는 not ready 또는 pending 상태를 노출한다.
3. consumer는 request를 보낸다.

검증:

- ready 전 provider는 routing 대상이 아니다.
- ready 후 provider는 routing 대상에 포함된다.
- readiness 전 request는 다른 ready provider로 가거나 명확한 unavailable error를 낸다.

## DSC-006 graceful drain

우선순위: `P1`

절차:

1. provider A, B를 시작한다.
2. B에 drain 신호를 준다.
3. B는 새 request를 받지 않고 기존 request만 끝낸다.
4. drain 완료 뒤 B를 종료한다.

검증:

- drain 상태 provider는 신규 routing에서 빠진다.
- 이미 처리 중인 request는 정상 응답하거나 정해진 cancellation error를 낸다.
- registry topology에는 drain 또는 not ready 상태 변화가 관측된다.

## DSC-007 cross-channel discovery

우선순위: `P1`

구성:

- API channel provider 2개
- workflow channel provider 2개
- fanout publisher 1개

검증:

- 각 channel의 provider 집합이 서로 섞이지 않는다.
- 같은 endpoint host를 쓰더라도 channel name이 다르면 독립 topology로 관리된다.
- 한 channel scale-in이 다른 channel routing에 영향을 주지 않는다.

## DSC-008 지속 traffic 중 scale-out/scale-in

우선순위: `P0`

구성:

- registry 1개
- provider A, B, C
- consumer client 2개
- channel request payload는 request index와 client id를 포함한다.

절차:

1. provider A만 시작한다.
2. client 2개가 `requestToChannel`을 계속 보내되, 각 client는 동시에 처리 중인
   request를 1개로 제한한다.
3. traffic이 진행 중인 상태에서 provider B를 추가한다.
4. B가 topology에 반영된 뒤 provider C를 추가한다.
5. B와 C가 모두 routing 대상이 된 것을 확인한 뒤 provider A를 정상 종료한다.
6. topology가 provider B, C만 남은 상태를 반영할 때까지 기다린다.
7. 각 client가 추가로 10개 이상의 request를 보낸다.

검증:

- A만 있을 때는 A만 호출된다.
- B, C가 추가된 뒤에는 topology 반영 이후 검증 구간에서 B와 C가 모두 호출된다.
- A 종료 뒤에는 A로 신규 request가 가지 않는다.
- 전체 request 중 완료된 요청은 정확히 한 provider evidence에만 기록된다.
- timeout이나 retry가 발생해도 같은 request id가 두 provider에서 동시에 성공 처리되지 않는다.
- client request map의 pending count를 public 또는 test-only 관측 API로 볼 수 있으면
  테스트 종료 시 0임을 확인한다. 그런 API가 없는 언어는 traffic task 종료, runtime
  shutdown, 완료된 request id의 중복 없음으로 pending 누수를 간접 검증한다.

운영 부하에 가까운 검증은 `P2` 변형으로 둔다. 예를 들어 client 2개가 초당 50개씩
10초 동안 request를 보내는 동안 provider를 추가하고 제거한다. 이 변형은 release gate에
항상 넣기보다 장시간 회귀나 성능 검증에서 실행한다.

## DSC-009 같은 routing id, 다른 endpoint failover

우선순위: `P0`

이 시나리오는 Kubernetes에서 pod가 내려갔다가 새 pod로 올라오는 상황을 흉내 낸다.
서비스의 논리 routing id는 같지만 실제 endpoint는 바뀔 수 있다. 기본 정책이 routing id
기준 최신 endpoint로 덮어 쓰는 방식이라면 이 시나리오가 반드시 통과해야 한다.

구성:

- provider logical rid: `api-a`
- provider v1 endpoint: `tcp://127.0.0.1:<port1>`
- provider v2 endpoint: `tcp://127.0.0.1:<port2>`
- consumer client는 Discovery만 사용하고 endpoint를 직접 알지 않는다.

절차:

1. provider v1을 rid `api-a`, endpoint `<port1>`로 시작한다.
2. client가 `requestToChannel`을 보내 v1 evidence를 확인한다.
3. provider v1을 종료한다.
4. provider v2를 같은 rid `api-a`, endpoint `<port2>`로 시작한다.
5. registry topology가 rid `api-a`의 endpoint를 `<port2>`로 갱신했는지 확인한다.
6. client를 재시작하지 않고 다시 `requestToChannel`을 보낸다.

검증:

- topology에는 rid `api-a`가 하나만 존재한다. v1과 v2가 중복 provider로 남지 않는다.
- v2 시작 뒤 신규 request는 `<port2>` provider evidence에 기록된다.
- client는 `<port1>` stale endpoint로 반복 timeout을 만들지 않는다.
- 같은 rid 교체 중 in-flight request 결과는 `RES-001A`와 `RES-004A`에서 별도로
  검증한다. 이 시나리오는 교체 완료 뒤 신규 routing만 검증한다.
- 교체 완료 뒤 같은 client의 다음 20개 request는 모두 성공한다.

## DSC-010 같은 routing id endpoint 동시 경합

우선순위: `P1`

구성:

- provider old와 provider new가 같은 rid를 사용한다.
- old endpoint와 new endpoint가 짧은 시간 동시에 registry에 보일 수 있다.

절차:

1. old provider를 시작한다.
2. old가 drain 상태로 들어가기 전 new provider를 같은 rid와 다른 endpoint로 시작한다.
3. registry가 같은 rid에 대해 어떤 entry를 active로 보는지 조회한다.
4. client가 request를 계속 보낸다.

검증:

- 같은 rid에 대해 convergence 이후 client가 routing 대상으로 보는 endpoint는 하나여야
  한다.
- overlap 구간에서 old 또는 new 중 어느 쪽으로 request가 갈 수 있는지는 구현의 공개된
  registry 갱신 규칙을 따른다. 테스트는 특정 metadata 필드를 요구하지 않는다.
- old provider가 늦게 unregister해도 convergence 이후 선택된 new endpoint를 지우지
  않는다.
- client는 같은 rid의 stale endpoint로 무기한 retry하지 않는다.

## DSC-011 endpoint 재사용, 다른 routing id

우선순위: `P1`

구성:

- provider A rid `api-a`, endpoint `<port1>`
- provider B rid `api-b`, endpoint `<port1>`을 재사용하는 변형

절차:

1. provider A를 시작한 뒤 종료한다.
2. 같은 endpoint 주소를 provider B가 다른 rid로 사용한다.
3. client가 target rid 또는 channel routing 정책에 따라 request를 보낸다.

검증:

- endpoint 문자열만 같다는 이유로 provider identity가 섞이지 않는다.
- rid가 다르면 registry entry도 다르다.
- A의 stale metadata가 B request evidence에 섞이지 않는다.

## DSC-012 provider flapping

우선순위: `P1`

절차:

1. provider B를 5초 동안 3회 반복해서 시작/종료한다.
2. provider A는 계속 ready 상태로 둔다.
3. client는 초당 20개 request를 계속 보낸다.

검증:

- B가 ready일 때는 일부 요청을 받는다.
- B가 down일 때는 A가 요청을 처리한다.
- B flapping 때문에 client 전체가 unavailable 상태가 되지 않는다.
- registry topology update가 누적되어 provider count가 계속 증가하지 않는다.

## DSC-013 stale topology snapshot 보호

우선순위: `P1`

절차:

1. client가 topology snapshot v1을 받아 provider B endpoint를 안다.
2. provider B가 종료되고 topology snapshot v2에서 제거된다.
3. topology convergence가 완료되기 전에 client가 즉시 request를 보낸다.
4. topology convergence 이후 다시 request를 보낸다.

검증:

- scheduler는 send 직전 provider state를 다시 확인하거나, 실패한 endpoint를 즉시
  stale 처리한다.
- 같은 stale endpoint로 연속 request를 보내지 않는다.
- 다음 request는 최신 snapshot의 ready provider로 간다.

## DSC-014 multi-client topology convergence

우선순위: `P1`

구성:

- consumer client 5개
- provider A, B

절차:

1. 모든 client가 provider A, B를 discovery한다.
2. provider B를 종료한다.
3. 각 client가 topology 변경을 반영하는 시간을 측정한다.

검증:

- 모든 client가 정해진 convergence timeout 안에 B 제거를 반영한다.
- 어떤 client도 B 제거 후 5개를 초과하는 stale request를 만들지 않는다.
- convergence timeout과 stale request 허용치는 언어별 테스트 metadata에 남긴다.

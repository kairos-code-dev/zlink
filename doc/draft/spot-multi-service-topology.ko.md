[스펙 목차](../README.ko.md)

# Draft -- SPOT Node Channel Topology

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 앞선 "하나의 `SpotNode` 아래에 여러 channel attachment를 붙이는
모델"을 버리고, 더 단순한 방향으로 SPOT과 channel socket 토폴로지를 다시
정리한다.

현재 구현은 channel 호출 표면에는 `channel` 용어를 쓰지만, Discovery 생성자,
SPOT topic publish/subscribe, topology snapshot 구조체처럼 기존 공개 계약이 이미
널리 쓰이던 영역은 아직 `service_name` 필드 이름을 그대로 유지한다. 이 문서는
새로 정리한 SPOT channel topology의 의미를 설명하는 초안이며, 공개 헤더에 남아
있는 `service_name` 표기는 "현재 구현 이름"으로 읽어야 한다.

이번 초안의 목표는 아래와 같다.

- `SpotNode`가 아니라 attach된 `Discovery`가 channel view를 소유하게 한다.
- `SpotNode`의 `ROUTER`는 같은 channel의 다른 `SpotNode`와만 연결되게 한다.
- channel 단위 부하 분산은 `ROUTER`가 아니라 **`DEALER(client)` ->
  `ROUTER(server)`** 모델로 설명한다.
- `Spot` direct 전송과 channel 단위 요청을 서로 다른 개념으로 분리한다.
- `SpotNode`에 외부 socket을 범용 attach하는 모델을 이 초안 범위에서 제외한다.

이 문서는 "한 node가 여러 channel을 동시에 소유한다"는 모델이 아니라,
"각 연결 관계가 attach된 Discovery의 channel view를 따른다"는 모델을 기준으로
잡는다.

## 2. 왜 방향을 바꾸는가

이전 draft는 `SpotNode`를 channel-neutral hub처럼 다루려 했다. 이 방향은 아래
문제를 만들었다.

- `spot_rid -> owner_node_rid` 해석이 흔들린다.
- `SpotNode`가 어느 channel에 속하는지 분명하지 않다.
- 같은 node에 붙은 여러 `ROUTER` 중 어느 경로가 실제 목적지 주소를 대표하는지
  닫히지 않는다.
- attach한 외부 socket을 그 node 전용으로 써야 하는지, 다른 곳에서도 같이 써도
  되는지 규칙이 흐려진다.
- channel 단위 round-robin과 특정 목적지 direct addressing이 한 모델 안에서
  서로 충돌한다.

이 문제는 세 가지를 한 번에 하려 했기 때문에 생긴다.

- `SpotNode`의 정체성을 channel에서 떼어 내기
- channel 단위 load balancing 넣기
- direct routed addressing 유지하기

이 초안은 이 세 가지를 한 구조에 억지로 섞지 않는다.

## 3. 핵심 모델

새 기준 모델은 아래와 같다.

```text
+--------------------+    +--------------------+
| SpotNode A         |    | SpotNode B         |
|--------------------|    |--------------------|
| attach discovery   |    | attach discovery   |
| view = orders      |    | view = orders      |
| router (server)    |<-->| router (server)    |
| pub/sub mesh       |<-->| pub/sub mesh       |
| one Spot facade    |    | one Spot facade    |
+--------------------+    +--------------------+

+--------------------+    +--------------------+
| Dealer client      |--->| Router server set  |
|--------------------|    | for channel orders |
| attach discovery   |    |                    |
| view = orders      |    | server role        |
+--------------------+    +--------------------+
```

핵심 원칙은 아래와 같다.

- `channel_name`의 소유자는 `SpotNode`가 아니라 attach된 `Discovery`다.
- `SpotNode.router`는 같은 channel의 다른 `SpotNode.router`와만 연결된다.
- `SpotNode.router`는 계속 peer `SpotNode`와 연결되지만, 공개 `Spot` API에서
  `dest_node_rid + dest_spot_rid` direct send/request는 제거 대상으로 둔다.
- channel 단위 요청/분산 전송은 `DEALER(client)`가 `ROUTER(server)` 집합에
  보내는 방식으로 푼다.
- channel 단위 API는 기본적으로 "channel에 보낸다"는 뜻이지, "특정 서버를
  찍는다"는 뜻이 아니다.

## 4. SpotNode 모델

### 4.1 Channel View

이 초안에서는 `SpotNode`가 channel 이름을 직접 소유하지 않는다.

- `channel_name`은 attach된 `Discovery`가 가진다.
- `SpotNode`는 attach된 discovery view를 따라 같은 channel의 peer를 찾는다.
- node의 `routing_id`는 "어느 node인가"를 가리키는 논리 주소다.
- 그 node 위에 올라가는 `Spot`도 attach된 discovery view를 통해 channel 평면에
  참여한다.

즉 `SpotNode`는 channel 이름의 owner가 아니라, channel-aware runtime owner다.

### 4.2 내부 역할

`SpotNode`는 아래 책임을 가진다.

- attach된 discovery가 가리키는 같은 channel의 다른 `SpotNode`와 mesh를 만든다.
- 내부 peer routed 전달 경로를 제공한다.
- 자기 channel의 pub/sub mesh를 관리한다.
- discovery, admission, monitor 같은 node 운영 상태를 관리한다.

반대로 아래 책임은 `SpotNode`의 기본 책임에서 뺀다.

- 다른 channel용 external socket owner
- 여러 channel attachment table owner
- channel-neutral ingress hub

### 4.3 Discovery attach 규칙

`SpotNode`의 mesh 범위는 attach된 SPOT channel `Discovery`가 결정한다. 이 규칙은
아래처럼 고정한다.

- `SpotNode` mesh용 `Discovery`는 `ZLINK_CHANNEL_TYPE_SPOT` view만 허용한다.
- 같은 `SpotNode`에는 active SPOT channel view를 하나만 둘 수 있다.
- 두 번째 SPOT channel `Discovery` attach는 `EBUSY`로 실패한다.
- attach된 `Discovery`를 destroy하면 그 view가 공급하던 automatic peer set도 함께
  빠져야 한다.
- 기존 SPOT channel `Discovery`를 교체하려면 먼저 기존 `Discovery`를 destroy해
  active view를 비워야 한다.
- active view가 비워진 뒤에는 다른 SPOT channel `Discovery`를 다시 attach할 수
  있다.
- SPOT channel `Discovery`가 없는 `SpotNode`는 자동 연결을 하지 않는다.

즉 `SpotNode`의 channel 정체성은 생성자가 아니라 attach된 SPOT channel
`Discovery`가 닫는다.

### 4.4 수동 연결

SPOT channel `Discovery`가 없는 경우에는 수동 연결만 허용할 수 있다.

- 호출자는 `zlink_spot_node_connect_peer()` 같은 수동 경로로 peer를 연결한다.
- 이 경우 라이브러리는 상대가 어느 channel에 속하는지 자동으로 판별하지 않는다.
- 호출자가 맞는 category의 `SpotNode`끼리 연결해야 한다.
- 수동 연결과 discovery 자동 연결을 같은 peer 관계에 동시에 섞지 않는 것을 기본
  규칙으로 둔다.

즉 수동 모드에서는 channel categorizing 책임이 호출자에게 남는다.

## 5. Routed 모델

### 5.1 SpotNode peer routed 경로

`SpotNode.router`는 계속 같은 channel의 다른 `SpotNode.router`와 peer routed
경로를 만든다.

- 내부 전송 경로는 local runtime -> local `SpotNode.router` ->
  remote `SpotNode.router` -> remote runtime이다.
- 이 경로는 attach된 discovery가 같은 channel view로 묶는 `SpotNode` 사이에서만
  성립한다.
- 다만 이 draft는 이 경로를 공개 `Spot` direct API로 노출하지 않는다.

즉 peer routed 경로는 내부 topology와 node 간 전달 경로로는 유지하지만, 호출자가
`dest_node_rid + dest_spot_rid`를 직접 넣는 공개 `Spot` API는 제거 대상으로 본다.

### 5.2 Channel 단위 경로

channel 단위 request/send는 direct routed 경로와 다른 개념이다.

- channel 단위 경로:
  같은 channel의 server 집합 중 하나에 보낸다.

이 초안은 channel 단위 경로를 `ROUTER <-> ROUTER` 직접 분산 모델로 설명하지
않는다. 대신 아래 모델을 쓴다.

- `ROUTER`는 server 역할이다.
- `DEALER`는 client 역할이다.
- channel 단위 round-robin은 client `DEALER`가 server `ROUTER` 집합을 보고
  수행한다.

이 규칙이 필요한 이유는 아래와 같다.

- round-robin은 "누가 요청을 보낼 쪽인가"가 먼저 분명해야 한다.
- `ROUTER <-> ROUTER`만으로는 channel load balancer 의미를 닫기 어렵다.
- node peer routed topology와 channel load balancing은 전송 의미가 다르다.

### 5.3 Channel request/reply 귀속

channel request는 제출 시 선택된 `DEALER` 한 개에 귀속된다.

- request 제출 시 attach된 `DEALER`가 하나 선택된다.
- reply는 같은 request를 보낸 그 `DEALER` 경로로만 돌아와야 한다.
- reply를 다시 `channel_name`으로 재탐색하지 않는다.
- `SpotNode.router` 경로는 channel reply 경로에 참여하지 않는다.

즉 channel request/reply는 "channel 집합을 향해 시작하지만, 실제 in-flight request는
선택된 client 경로 하나에 귀속된다"는 규칙을 가진다.

### 5.4 특정 서버 직접 지정

이 초안의 기본 channel API는 특정 서버를 직접 지정하지 않는다.

- `send_channel("orders", ...)`
- `request_channel("orders", ...)`

이 함수의 의미는 "orders channel 처리자 중 하나에게 보낸다"이다.

특정 server `ROUTER`를 직접 지목하는 기능이 정말 필요하면, 그 기능은 별도의
저수준 socket API 또는 운영용 API로 다룬다. `Spot` high-level API에 기본
동작으로 섞지 않는다.

이렇게 나누는 이유는 channel 요청의 의미를 단순하게 유지하기 위해서다.

### 5.5 Channel API 실패 규칙 초안

`zlink_spot_send_channel()` / `zlink_spot_request_channel()`의 실패 의미는 아래처럼
고정하는 것이 맞다.

- 해당 `channel_name`에 대응하는 attach `DEALER`가 없음:
  `ENOENT`
- attach `DEALER`는 있으나 현재 전송 가능한 peer가 없음:
  `ENOTCONN`
- attach `DEALER`가 draining/admission 정책으로 현재 제출 불가:
  `EHOSTUNREACH` 또는 admission 전용 errno
- 잘못된 인자:
  `EINVAL` 또는 `EFAULT`

정확한 errno 매핑은 구현 단계에서 `errno-map` 문서와 함께 최종 확정한다.

## 6. Pub/Sub 모델

### 6.1 SpotNode pub/sub mesh

`SpotNode`의 pub/sub도 attach된 discovery가 같은 channel view로 묶는 다른
`SpotNode`와만 연결한다.

- node의 pub/sub mesh는 자기 channel 평면 안에서만 돈다.
- 응용이 publish할 때는 `Spot` facade를 사용한다.
- 수신도 `Spot` facade를 통해 받는다.

즉 pub/sub도 routed와 마찬가지로 "같은 channel 안의 node mesh"가 기본이다.

### 6.2 외부 pub/sub attach 제외

이 초안은 `SpotNode`에 외부 `PUB`/`SUB`를 범용 attach하는 모델을 채택하지
않는다.

그 이유는 아래와 같다.

- 소켓 소유권과 생명 주기가 흐려진다.
- node 전용 경로와 외부 공유 소켓이 섞인다.
- 디버깅용 관찰과 실제 데이터 평면 입력이 같은 표면에 섞인다.

따라서 이 초안 범위에서는 아래 기능을 제외한다.

- `SpotNode`에 external `PUB/SUB` 범용 attach
- 하나의 node가 여러 channel의 `PUB/SUB`를 동시에 소유하는 모델

### 6.3 외부 입력과 디버깅 관찰

응용이 요구하는 아래 기능은 separate draft로 다루는 것이 맞다.

- 외부 `PUB` 또는 producer가 `Spot` 쪽으로 topic을 넣는 입력 bridge
- node에 들어오는 topic을 읽는 디버깅용 tap 또는 monitor

이 기능은 범용 `attach_pubsub()`가 아니라, 목적이 분명한 별도 표면으로 두는
것을 원칙으로 한다.

- 입력 경로라면 ingress bridge
- 읽기 관찰이라면 read-only tap 또는 monitor

즉 "실제 데이터 평면 입력"과 "디버깅 관찰"을 같은 API로 묶지 않는다.

### 6.4 일반 PUB 에서 Spot 으로의 topic 입력

이 초안은 `SpotNode` pub/sub mesh를 같은 channel의 다른 `SpotNode`와만 연결하는
원칙을 유지한다. 다만 이것만으로는 "일반 `PUB`에서 `Spot`으로 topic을 넣고
싶다"는 요구를 풀 수 없다.

이 요구는 peer mesh가 아니라 **external publish ingress**로 다뤄야 한다.

- 일반 `PUB`가 `Spot` 쪽으로 topic을 넣는 기능은 허용할 수 있다.
- 그러나 이 경로는 다른 `SpotNode`와의 peer pub/sub 연결과 같은 의미가 아니다.
- 이 경로는 mesh replication 경로가 아니라, 외부 publisher가 local `SpotNode`
  runtime으로 topic을 주입하는 단방향 입력 경로다.

따라서 이 초안은 아래 규칙을 둔다.

- `SpotNode`의 peer pub/sub 자동 연결 대상은 같은 channel의 다른 `SpotNode`
  pub/sub 뿐이다.
- 같은 `channel_name`을 가진 일반 `PUB` provider는 `SpotNode` mesh 자동 연결
  대상이 아니다.
- 일반 `PUB -> Spot` 입력은 명시적 attach 표면으로 분리한다.
- 이 ingress는 `SpotNode` 전용 자원이며, 일반 raw pub/sub peer와 같은 의미로
  섞어 쓰지 않는다.

내부 동작은 아래처럼 해석한다.

```text
external PUB -> ingress receiver -> local SpotNode topic path -> Spot recv
```

즉 외부 publisher가 넣은 topic도 최종적으로는 local `Spot` publish와 같은 내부
topic 경로로 올라가야 한다. 그래야 `Spot` 수신, local fanout, mesh forward 규칙을
한 곳에서 유지할 수 있다.

이때 forwarding 범위는 아래처럼 해석한다.

- ingress로 들어온 topic은 local `Spot` 수신 경로로 올라간다.
- 해당 `SpotNode`가 현재 mesh peer를 가지고 있으면 같은 channel peer로도 forward
  될 수 있다.
- 즉 ingress는 local-only debug path가 아니라, 정상 topic publish 입력 경로다.

이 기능은 범용 `attach_pubsub()`로 설명하지 않는다. 공개 표면은 이 문서의
C API 초안 절을 기준안으로 둔다.

## 7. Discovery와 역할 메타데이터

### 7.1 SPOT Node Channel

`SpotNode`를 위한 Discovery는 `ZLINK_CHANNEL_TYPE_SPOT`을 사용한다.

이 view는 아래 정보를 제공해야 한다.

- `channel_name`
- `channel_type = SPOT`
- `channel_role = SPOT`
- `routing_id`
- advertise endpoint
- admission 상태

같은 channel view의 두 `SpotNode`가 서로를 발견하면, pairwise initiator 규칙으로
한쪽만 connect를 시작하게 해야 한다.

이 문서에서는 이 역할을 server/client로 나누지 않는다.

- 두 node 모두 `SPOT` 역할이다.
- 차이는 영구 역할이 아니라, pair마다 계산되는 initiator 여부다.
- 추가 server/client role 메타데이터는 두지 않는다.

즉 SPOT node mesh는 "양쪽이 같은 역할을 가지되, connect 시작자는 쌍마다 하나만
정한다"는 모델이다.

중요한 점은 아래와 같다.

- `SpotNode` mesh discovery는 같은 `channel_name`의 **SPOT node peer**만 본다.
- 같은 `channel_name`의 일반 socket channel provider는 이 mesh view에 섞이지
  않는다.
- 특히 일반 `ROUTER`, 일반 `PUB`, 일반 `SUB`는 `SpotNode` peer mesh 자동 연결
  대상이 아니다.

### 7.2 Channel Socket

channel 단위 요청을 위한 socket family는 `ZLINK_CHANNEL_TYPE_SOCKET` 아래에서
역할을 분명히 나눈다.

- `ROUTER`는 server 역할
- `DEALER`는 client 역할
- `PUB`는 publish provider 역할
- `SUB`는 consume 역할

이 초안에서 중요한 점은 `ROUTER`와 `DEALER`를 단순 socket type으로만 보지
않는다는 점이다. channel 토폴로지에서는 이 둘이 **역할 구분**을 함께 뜻한다.

이 draft는 이를 위해 별도의 추가 role 필드를 더 두지 않는다. 기존
`ROUTER/DEALER/PUB/SUB` channel 역할 구분만으로 충분하다고 본다.

즉 같은 `channel_name` 안에서 아래 규칙을 기본값으로 둔다.

- `DEALER(client) -> ROUTER(server)`
- `SUB(consumer) -> PUB(provider)`
- `ROUTER(server) -> ROUTER(server)`는 channel LB 경로가 아니라 direct node
  또는 peer 경로다.

별도로, external publish ingress 같은 예외 입력 경로가 필요하면 이 경로는
기존 `PUB(provider)`와 같은 일반 channel pub/sub 의미로 다루지 않는다.
즉 ingress는 raw socket family의 일반 `SUB -> PUB` 자동 연결 규칙과 별도 표면으로
분리한다.

## 8. 소유권과 전용성 규칙

이 초안은 `SpotNode` 내부 socket을 node 전용 자원으로 본다.

- `SpotNode.router`는 그 node의 routed 경로에만 사용한다.
- `SpotNode` pub/sub mesh socket도 그 node의 mesh 용도로만 사용한다.
- 같은 내부 socket을 다른 channel owner가 공유해서는 안 된다.
- attach된 `DEALER`와 ingress `PUB`도 같은 규칙을 따른다.

이 규칙을 공개 계약 수준에서 분명히 두는 이유는 아래와 같다.

- callback, poller, recv ownership을 단순하게 유지할 수 있다.
- monitor와 lifecycle 책임이 한 owner에 고정된다.
- backpressure와 admission 해석이 channel별로 섞이지 않는다.

즉 이 초안에서 `SpotNode`는 재사용 가능한 generic socket 컨테이너가 아니다.

## 8.1 Attach cardinality 요약

구현 기준 cardinality는 아래처럼 잡는다.

- SPOT channel `Discovery`: node당 active view 1개
- channel `DEALER`: channel view당 1개
- ingress `PUB`: node당 1개
- `Spot` facade: 기존 공개 계약을 유지하되, channel-aware 모델과 충돌하지 않게
  한다

이 cardinality를 넘는 attach는 기본적으로 `EBUSY`로 실패한다.

## 8.2 자동 연결과 수동 연결 제약

이 초안은 자동 연결과 수동 연결을 아래처럼 구분한다.

### 8.2.1 자동 연결 범위

SPOT mesh 자동 연결은 아래 조건을 모두 만족하는 peer에만 적용한다.

- 같은 `channel_name`
- `ZLINK_CHANNEL_TYPE_SPOT`
- 다른 `SpotNode`

즉 자동 연결은 아래 대상을 절대 포함하지 않는다.

- 같은 `channel_name`의 일반 `ROUTER`
- 같은 `channel_name`의 일반 `PUB`
- 같은 `channel_name`의 일반 `SUB`
- cross-channel peer

### 8.2.2 수동 연결 함수 제약

수동 연결 함수는 `SpotNode` mesh 수동 연결에만 쓴다.

- `zlink_spot_node_connect_peer()`는 `SpotNode` peer endpoint를 직접 연결하는
  함수다.
- 이 함수는 일반 socket service의 `ROUTER`/`PUB`/`SUB` provider를 붙이는 용도로
  쓰지 않는다.
- 다른 channel 호출은 자동 연결이면
  `zlink_spot_node_attach_channel_dealer()`, 수동 연결이면
  `zlink_spot_node_attach_channel_dealer_manual()`과 attach된 `DEALER` 경로로만
  푼다.
- 일반 `PUB -> Spot` 입력은 `zlink_spot_node_attach_pub_ingress()`로만 푼다.

즉 아래 조합은 계약 위반으로 본다.

- channel 호출을 `zlink_spot_node_connect_peer()`로 우회하기
- 일반 socket service를 `SpotNode` peer처럼 connect하기
- `SpotNode.router`를 channel 호출 경로에 재사용하기

### 8.2.3 자동 연결과 수동 연결 혼합

같은 peer 관계에 자동 연결과 수동 연결을 동시에 섞지 않는다.

- SPOT channel `Discovery`가 관리하는 peer를 같은 node에서 다시 수동 connect하지
  않는다.
- 이미 discovery가 관리하는 관계에 수동 `disconnect/connect`를 겹치게 쓰면
  관찰 결과와 ownership이 흐려진다.
- 구현은 가능하면 이런 중복 시도를 `EBUSY` 또는 `EINVAL`로 거부하는 쪽이 맞다.

정확한 거부 errno는 구현 단계에서 정하되, "같은 관계를 자동/수동으로 동시에
관리하지 않는다"는 정책은 이 draft에서 고정한다.

## 9. 공개 surface 방향

이 초안은 아래 방향을 채택한다.

### 9.1 C API 초안

이 절의 함수 시그니처는 구현 전 논의용 초안이다. 현재 공개 헤더에 없는 함수는
이 문서만으로 계약이 확정된 것이 아니다.

#### 9.1.1 SpotNode 생성과 Discovery Attach

이 초안에서는 `SpotNode`가 생성 시점에 `channel_name`을 직접 받지 않는다.
channel view는 attach된 `Discovery`가 가진다.

```c
void *zlink_spot_node_new (
  void *ctx);

zlink_config_result_t zlink_spot_node_attach_discovery (
  void *node,
  void *discovery);
```

의미는 아래와 같다.

- `SpotNode` 생성 자체는 channel-neutral 하다.
- `Discovery`가 channel view를 가진다.
- attach된 discovery의 `channel_name`이 이 node의 mesh auto-connect 범위를
  결정한다.
- 같은 `SpotNode`에는 active SPOT channel view를 하나만 둘 수 있다.
- 두 번째 SPOT channel `Discovery` attach는 `EBUSY`로 실패한다.
- `zlink_spot_node_attach_discovery()`는 `ZLINK_CHANNEL_TYPE_SPOT`만 허용한다.
- 다른 channel type `Discovery`를 넘기면 `EINVAL`로 실패한다.

#### 9.1.2 제거 대상 Spot Direct API

이 draft는 아래 `Spot` direct API를 제거 대상으로 둔다.

```c
zlink_submit_result_t zlink_spot_send_spot (
  void *spot,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_spot_request_spot (
  void *spot,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

제거 이유는 아래와 같다.

- `Spot` 공개 API에서 rid direct addressing을 유지하면 channel 호출 모델과
  의미가 충돌한다.
- `SpotNode.router`는 node peer topology를 위해 남기되, 호출자가 직접 rid를 넣는
  경로는 공개 high-level surface에서 빼는 것이 맞다.
- 특정 rid 대상 전송이 꼭 필요하면 low-level socket API에서 다루는 쪽이 낫다.

#### 9.1.3 다른 Channel 호출용 DEALER Attach

`Spot`이 다른 channel을 channel 단위로 호출해야 할 때는 두 경로를 모두 둔다.

- 자동 연결 경로:
  channel view를 가진 `Discovery`와 함께 attach
- 수동 연결 경로:
  `channel_name`을 직접 주고 attach

```c
zlink_config_result_t zlink_spot_node_attach_channel_dealer (
  void *node,
  void *discovery,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual (
  void *node,
  const char *channel_name,
  void *dealer);
```

수동 연결관리 편의를 위해 socket-level channel metadata API도 함께 둘 수 있다.

```c
zlink_config_result_t zlink_socket_set_channel_name (
  void *socket,
  const char *channel_name);

zlink_config_result_t zlink_socket_get_channel_name (
  void *socket,
  char *channel_name_buf,
  size_t channel_name_capacity,
  size_t *channel_name_len_out);
```

의미는 아래와 같다.

- `zlink_spot_node_attach_channel_dealer()`는 자동 연결용 표면이다.
- `discovery`는 호출 대상 channel의 view를 가진다.
- `discovery`는 `ZLINK_CHANNEL_TYPE_SOCKET` view여야 한다.
- `zlink_spot_node_attach_channel_dealer_manual()`은 수동 연결용 표면이다.
- `channel_name`은 호출 대상 channel 이름이다.
- 두 함수 모두 `dealer`는 해당 channel의 `ROUTER(server)` 집합에 붙는 client
  socket이다.
- 같은 `SpotNode`에 서로 다른 channel의 `DEALER`는 여러 개 붙일 수 있다.
- 같은 channel을 뜻하는 `DEALER`를 두 번 attach하면 `EBUSY`로 실패한다.
- 이 중복 규칙은 자동 attach와 수동 attach 사이에도 동일하게 적용한다.
- 즉 같은 `channel_name`에 자동 attach된 `DEALER`가 있으면 같은 channel에 대한
  수동 attach는 `EBUSY`다. 반대 경우도 동일하다.
- attach된 `DEALER`는 `SpotNode` 전용 자원이다.
- attach 후에 응용이 같은 `DEALER`를 일반 socket처럼 다른 owner에서 함께
  사용해서는 안 된다.
- attach는 socket 소유권을 가져오지 않는다. destroy 책임은 호출자에게 남는다.
- runtime 중 `detach` 공개 API는 두지 않는다.
- 자동 연결 표면에서 `discovery`가 socket channel view가 아니면 `EINVAL`로
  실패한다.
- 수동 연결 표면에서 `channel_name`이 비어 있거나 잘못되면 `EINVAL`로 실패한다.
- 두 표면 모두 `dealer`가 `DEALER`가 아니면 `EINVAL`로 실패한다.

socket-level channel metadata는 아래처럼 해석한다.

- setter는 socket에 fixed logical channel name을 기록하는 편의 기능이다.
- setter는 connect, bind, discovery attach를 자동으로 수행하지 않는다.
- getter는 현재 socket에 기록된 logical channel name을 돌려준다.
- socket metadata가 비어 있으면 getter는 `ENOENT`다.
- attach는 여전히 최종 검증 지점이다.

attach와 metadata가 함께 있을 때 규칙은 아래처럼 둔다.

- discovery attach에서 socket metadata가 비어 있으면 discovery channel로 귀속된다.
- discovery attach에서 socket metadata가 discovery channel과 같으면 허용한다.
- discovery attach에서 socket metadata가 discovery channel과 다르면 `EINVAL`이다.
- manual attach에서 socket metadata가 비어 있으면 attach 인자의 `channel_name`으로
  귀속된다.
- manual attach에서 socket metadata가 attach 인자와 같으면 허용한다.
- manual attach에서 socket metadata가 attach 인자와 다르면 `EINVAL`이다.

즉 socket metadata는 attach를 대신하는 값이 아니라, 수동 연결관리에서 channel
귀속을 미리 고정하고 이후 attach 검증과 dispatch source 식별에 재사용하는 값이다.

attach 의미를 더 분명히 하기 위해 아래 규칙도 고정한다.

- attach 함수는 socket을 생성하지 않는다.
- attach 함수는 socket의 connect를 대신 수행하지 않는다.
- 자동 연결 경로에서는 discovery-managed `DEALER`를 넘긴다.
- 수동 연결 경로에서는 호출자가 직접 `connect`를 끝낸 `DEALER`를 넘긴다.
- attach하는 `DEALER`의 target `channel_name`이 `SpotNode`의 SPOT channel 이름과
  같아도 계약 위반은 아니다.
- 다만 예시에서는 mesh channel과 channel call 대상을 헷갈리지 않게 하기 위해
  다른 channel 이름을 사용한다.

예시는 아래와 같다.

자동 연결 경로:

```c
/* node itself participates in the "alpha" SPOT channel */
void *alpha_spot_discovery =
  zlink_discovery_new(ctx, ZLINK_CHANNEL_TYPE_SPOT, "alpha");
zlink_discovery_connect_registry(alpha_spot_discovery, "tcp://registry:5551");
zlink_spot_node_attach_discovery(node, alpha_spot_discovery);

/* cross-channel call target: separate SOCKET channel "orders" */
void *orders_discovery =
  zlink_discovery_new(ctx, ZLINK_CHANNEL_TYPE_SOCKET, "orders");
zlink_discovery_connect_registry(orders_discovery, "tcp://registry:5551");

void *orders_dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_socket_attach_discovery(orders_dealer, orders_discovery);

zlink_spot_node_attach_channel_dealer(node, orders_discovery, orders_dealer);
```

- `alpha_spot_discovery`는 이 `SpotNode` 자신의 peer mesh를 정한다.
- `orders_dealer`의 peer set은 discovery view가 관리한다.
- `orders_discovery`는 `SpotNode` 자신의 channel이 아니라, **다른 channel 호출 대상**
  view다.
- attach는 이 cross-channel dealer를 `SpotNode`의 channel call 경로에 등록하는
  일이다.

수동 연결 경로:

```c
/* node itself participates in the "alpha" SPOT channel */
void *alpha_spot_discovery =
  zlink_discovery_new(ctx, ZLINK_CHANNEL_TYPE_SPOT, "alpha");
zlink_discovery_connect_registry(alpha_spot_discovery, "tcp://registry:5551");
zlink_spot_node_attach_discovery(node, alpha_spot_discovery);

/* cross-channel call target: manual SOCKET channel "orders" */
void *orders_dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_connect(orders_dealer, "tcp://orders-a:7001");
zlink_connect(orders_dealer, "tcp://orders-b:7001");

zlink_spot_node_attach_channel_dealer_manual(
  node, "orders", orders_dealer);
```

- connect는 호출자가 먼저 끝낸다.
- `orders`는 이 `SpotNode` 자신의 channel이 아니라, **다른 channel 호출 대상**
  이름이다.
- attach는 이미 준비된 cross-channel dealer를 `channel_name` 아래 등록하는 일이다.

#### 9.1.4 Spot에서 다른 Channel로 보내는 API

attach된 `DEALER`를 쓰는 channel 단위 API는 아래처럼 잡을 수 있다.

```c
zlink_submit_result_t zlink_spot_send_channel (
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_spot_request_channel (
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

이 함수의 의미는 아래와 같다.

- `Spot`이 자기 channel 밖의 다른 channel을 호출한다.
- 실제 전송은 `channel_name`에 대응하는 attach `DEALER`를 통해 나간다.
- 이 lookup은 자동 attach면 `Discovery.channel_name`, 수동 attach면 등록된
  `channel_name` 기준으로 수행한다.
- 목적지는 특정 server가 아니라, 해당 channel의 `ROUTER(server)` 집합 중 하나다.
- 특정 server를 직접 찍는 기능은 이 high-level API에 넣지 않는다.
- channel 호출은 `DEALER` 경로로만 가능하다.
- `SpotNode.router` 경로를 channel 호출에 재사용하지 않는다.

#### 9.1.5 일반 PUB 에서 Spot 으로의 ingress

일반 `PUB -> Spot` 입력은 peer mesh가 아니라 별도 ingress 표면으로 분리한다.

```c
zlink_config_result_t zlink_spot_node_attach_pub_ingress (
  void *node,
  void *pub);
```

의미는 아래와 같다.

- 이 `pub`는 `Spot` runtime으로 topic을 넣기 위한 전용 publish source다.
- 호출자는 이 `pub`로 topic을 publish하고, `SpotNode`는 그 publish를 자기 local
  topic 경로로 끌어올린다.
- 이 ingress는 같은 channel `SpotNode` peer mesh와 다른 의미의 경로다.
- 이 `pub` 역시 `SpotNode` 전용 자원으로 다뤄야 한다.
- 같은 `SpotNode`에 `pub ingress`는 하나만 둘 수 있다.
- 두 번째 `zlink_spot_node_attach_pub_ingress()`는 `EBUSY`로 실패한다.
- attach는 socket 소유권을 가져오지 않는다. destroy 책임은 호출자에게 남는다.
- runtime 중 `detach` 공개 API는 두지 않는다.
- `pub`가 `PUB`가 아니면 `EINVAL`로 실패한다.

내부 구현은 node가 소유하는 ingress receiver를 별도로 둘 수 있다. 다만 그 내부
receiver는 공개 계약의 중심이 아니다. 공개 표면의 의미는 "호출자가 publish할 수
있는 전용 `PUB`를 `SpotNode` 입력 경로에 연결한다"는 데 있다.

구현 제약은 아래처럼 고정한다.

- attach 시 라이브러리는 node 전용 hidden ingress receiver를 내부 생성할 수 있다.
- 이 hidden receiver는 공개 API에 노출하지 않는다.
- attach 대상 `PUB`는 attach 시점에 아직 이 ingress 용도로 사용되지 않은 socket
  이어야 한다.
- attach 함수가 기존 `PUB`의 외부 transport `connect/bind` 상태를 해석하거나
  재배선하지는 않는다.
- attach 후 이 `PUB`는 `SpotNode` ingress 전용 publish source로만 사용한다.
- attach 함수는 hidden receiver와 `PUB`를 node 내부 경로로 연결하는 역할만 한다.

이 문서는 이 공개 이름을 `zlink_spot_node_attach_pub_ingress()`로 유지하는 쪽을
기준안으로 둔다.

#### 9.1.6 제외하는 표면

이 draft는 아래 같은 multi-channel attachment 표면을 채택하지 않는다.

```c
zlink_config_result_t zlink_spot_node_attach_router (
  void *node,
  const char *channel_name,
  void *router);

zlink_config_result_t zlink_spot_node_attach_pubsub (
  void *node,
  const char *channel_name,
  void *pub,
  void *sub);
```

이 함수군은 "하나의 `SpotNode`가 여러 channel attachment를 소유한다"는 이전
모델에 더 가깝다. 현재 draft의 기준 모델과는 맞지 않는다.

#### 9.1.7 Attach 함수 실패 규칙 초안

attach 계열 함수의 실패 규칙은 아래처럼 고정하는 것이 맞다.

- handle 인자가 `NULL`이거나 타입이 맞지 않음:
  `EINVAL` 또는 `EFAULT`
- 같은 자리에 허용된 cardinality를 넘는 중복 attach:
  `EBUSY`
- 잘못된 channel type `Discovery`:
  `EINVAL`
- 수동 attach에서 비어 있거나 잘못된 `channel_name`:
  `EINVAL`
- 잘못된 socket type (`DEALER` 자리에 다른 socket, `PUB ingress` 자리에 다른
  socket):
  `EINVAL`
- 이미 다른 owner에 전용 attach된 socket을 다시 attach:
  `EBUSY`

#### 9.1.8 수동 연결 함수 실패 규칙 초안

수동 연결 함수의 실패 규칙은 아래처럼 정리한다.

- `zlink_spot_node_connect_peer()`에 `NULL` node 또는 잘못된 endpoint:
  `EINVAL` 또는 `EFAULT`
- SPOT channel `Discovery`가 이미 관리하는 peer 관계를 같은 node에서 다시 수동
  연결:
  `EBUSY` 또는 `EINVAL`
- `zlink_spot_node_attach_channel_dealer_manual()`에 비어 있거나 잘못된
  `channel_name`:
  `EINVAL`
- `zlink_spot_node_attach_channel_dealer_manual()`에 `DEALER`가 아닌 socket:
  `EINVAL`
- context 종료 상태:
  `ETERM`

이 함수는 peer endpoint 문자열만 받기 때문에, 상대가 실제 `SpotNode`인지 일반
socket service인지 완전히 검증하지 못할 수 있다. 따라서 "같은 category의
`SpotNode`끼리만 수동 연결한다"는 계약 책임은 호출자에게 남는다.

### 9.2 유지할 개념

- `SpotNode` 생성/파괴
- 같은 channel `SpotNode` 자동 연결
- `Spot` facade
- 자동/수동 두 경로의 channel `DEALER` attach
- `DEALER(client) -> ROUTER(server)` channel 단위 요청
- node monitor / channel monitor / admission

### 9.3 제외할 개념

- 하나의 `SpotNode` 아래 여러 channel attachment table
- `zlink_spot_node_attach_router()` 식의 multi-channel manual attach
- `zlink_spot_node_attach_pubsub()` 식의 multi-channel pub/sub attach
- `Spot` 하나에서 여러 channel 이름을 직접 다루는 channel-neutral hub 모델
- `Spot`의 `dest_node_rid + dest_spot_rid` direct send/request surface

이 초안은 "멀티채널을 한 `SpotNode` 안에 넣는 것"이 아니라, "채널마다
자기 연결 view와 자기 role 토폴로지를 가진다"는 방향을 채택한다.

## 10. 권장 사용 흐름

### 10.1 같은 Channel의 SpotNode Peer 통신

```text
local runtime -> local SpotNode.router
              -> remote SpotNode.router -> remote runtime
```

이 경로는 같은 channel 안의 `SpotNode` peer topology를 설명하는 내부 경로다.
이 draft에서는 이 경로를 호출자가 직접 `Spot` API로 지목하지 않는다.

### 10.2 Channel 단위 요청

```text
client DEALER -> one server ROUTER from channel pool
```

이 경로는 특정 server를 직접 고르지 않고, channel 처리자 집합 중 하나에 보내는
경로다. channel 호출은 항상 attach된 `DEALER`를 통해 처리한다.

### 10.3 topic 발행

```text
App -> Spot publish -> local SpotNode pub mesh
    -> remote SpotNode sub mesh -> remote Spot recv
```

이 경로도 같은 channel 안의 node mesh를 기준으로 해석한다.

## 10.4 잘못된 사용 예

아래 사용은 이 draft에서 금지한다.

- 같은 node에 SPOT channel `Discovery` 두 개 attach
- 같은 channel view의 `DEALER` 두 개 attach
- 같은 node에 `PUB ingress` 두 개 attach
- channel 호출을 `SpotNode.router` 또는 제거 대상 `zlink_spot_send_spot()` 계열로 대체
- 일반 `PUB/SUB/ROUTER` provider를 `SpotNode` peer mesh 자동 연결 대상처럼 취급
- 같은 peer 관계를 discovery 자동 연결과 수동 연결로 동시에 관리

## 11. 구현 전 확인할 항목

구현 전에는 아래 항목을 먼저 닫아야 한다.

- 디버깅 관찰을 monitor 확장으로 다룰지, 별도 debug tap 문서로 분리할지
- 기존 multi-channel attach 초안에서 남아 있는 API 이름을 어떻게 정리할지

이 항목이 닫히기 전에는 기존 정식 spec 문서에 이번 draft 내용을 섞어 넣지
않는다.

## 11.1 회귀 테스트 최소 범위

구현 후에는 아래 항목을 최소 회귀 범위로 둔다.

### 11.1.1 자동 연결

- 같은 `channel_name`의 두 `SpotNode`가 SPOT channel `Discovery` attach 후 서로
  자동 연결되는지 확인한다.
- 다른 `channel_name`의 `SpotNode`끼리는 자동 연결되지 않는지 확인한다.
- 같은 `channel_name`의 일반 `ROUTER/PUB/SUB` provider가 있어도 `SpotNode`
  mesh 자동 연결 대상에 섞이지 않는지 확인한다.
- active SPOT channel view를 가진 `SpotNode`에 두 번째 SPOT `Discovery` attach가
  `EBUSY`로 거부되는지 확인한다.

### 11.1.2 수동 연결

- `Discovery` 없는 두 `SpotNode`가 수동 `connect_peer`로 연결되는지 확인한다.
- 같은 category가 아닌 endpoint를 수동 연결했을 때 호출자 책임 범위가 문서와
  일치하는지 확인한다.
- discovery가 이미 관리하는 peer 관계를 같은 node에서 다시 수동 connect하려고
  할 때 거부되는지 확인한다.

### 11.1.3 Channel Dealer Attach

- 서로 다른 channel view의 `DEALER`를 같은 `SpotNode`에 여러 개 attach할 수
  있는지 확인한다.
- 같은 channel view의 `DEALER` 중복 attach가 `EBUSY`인지 확인한다.
- socket channel `Discovery`가 아닌 handle로 attach하려 할 때 `EINVAL`인지
  확인한다.
- `DEALER`가 아닌 socket을 attach하려 할 때 `EINVAL`인지 확인한다.
- attach된 `DEALER`를 다른 owner에 다시 attach하려 할 때 `EBUSY`인지 확인한다.
- 수동 attach 함수로 서로 다른 `channel_name`의 `DEALER`를 여러 개 붙일 수
  있는지 확인한다.
- 수동 attach 함수에서 같은 `channel_name` 중복 attach가 `EBUSY`인지 확인한다.
- 수동 attach 함수에서 비어 있거나 잘못된 `channel_name`이 `EINVAL`인지
  확인한다.

### 11.1.4 Channel API

- attach된 `DEALER`가 있는 channel에 `send_channel/request_channel`이 성공하는지
  확인한다.
- 해당 `channel_name`의 `DEALER`가 없을 때 `ENOENT`인지 확인한다.
- `DEALER`는 있으나 peer가 없을 때 `ENOTCONN`인지 확인한다.
- request reply가 제출에 사용한 같은 `DEALER` 경로로 돌아오는지 확인한다.
- channel 호출이 `SpotNode.router` 경로를 사용하지 않는지 확인한다.
- 자동 attach한 `DEALER`와 수동 attach한 `DEALER` 모두에서 동일한 channel API
  의미가 유지되는지 확인한다.

### 11.1.5 Pub Ingress

- `attach_pub_ingress()` 후 일반 `PUB`에서 publish한 topic이 local `Spot` recv로
  올라오는지 확인한다.
- mesh peer가 있을 때 ingress topic이 같은 channel peer로도 forward되는지
  확인한다.
- 두 번째 ingress `PUB` attach가 `EBUSY`인지 확인한다.
- `PUB`가 아닌 socket attach가 `EINVAL`인지 확인한다.

### 11.1.6 제거 대상 API

- `zlink_spot_send_spot()` / `zlink_spot_request_spot()`가 공개 surface에서 제거되거나
  deprecated 처리되는지 확인한다.
- 내부 `SpotNode.router` peer topology가 남아 있어도, channel 호출이 이 공개 API를
  통해 우회되지 않는지 확인한다.

### 11.1.7 제약 위반

- 금지된 multi-channel attach API를 호출했을 때 공개 surface에 노출되지 않거나,
  내부 테스트 표면에서는 명확히 거부되는지 확인한다.
- 같은 node에 자동 연결과 수동 연결을 동시에 겹치게 쓰는 시도가 거부되는지
  확인한다.
- 전용 attach 자원을 다른 owner에서 같이 쓰려는 시도가 거부되거나 계약 위반으로
  드러나는지 확인한다.

## 12. 정식 문서 반영 계획

구현과 공개 헤더가 정리되면 이 초안 내용은 아래 문서들로 나누어 반영한다.

- `doc/spec/core/service/spot*.md`
  `SpotNode`의 channel view, direct routed 경로, pub/sub mesh 모델
- `doc/spec/core/service/discovery*.md`
  `SPOT` mesh initiator 규칙, `ROUTER/DEALER/PUB/SUB` channel 역할 방향 규칙
- 필요하면 `doc/spec/core/socket/router*.md`
  channel server `ROUTER`와 direct addressing의 관계
- 필요하면 별도 draft 또는 guide
  external ingress bridge, debug tap/monitor

guide에는 아래를 넣는다.

- 언제 `Spot` publish/subscribe를 쓰는지
- 언제 channel client `DEALER`를 쓰는지
- 왜 channel API에서 특정 server 지정이 기본 기능이 아닌지
- 디버깅 관찰은 왜 monitor/tap으로 분리하는지

internals에는 아래를 넣는다.

- SPOT mesh socket 배선
- pairwise initiator 계산 방식
- channel client pool 관리와 admission 반영
- ingress bridge 또는 tap이 추가될 경우 내부 handoff 구조

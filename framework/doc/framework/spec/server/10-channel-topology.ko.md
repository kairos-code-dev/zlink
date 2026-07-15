<!-- framework-adapter-nav:start -->
[스펙 목차](../README.ko.md) | [이전: ZLink Framework API](../05-framework-api.ko.md) | [다음: Channel 메시징](11-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->


[문서 묶음](../../common/README.ko.md) | [개요](../01-overview.ko.md) | [상호작용 모델](../02-interaction-model.ko.md) | [메시지 모델](../03-message-model.ko.md) | [framework API](../05-framework-api.ko.md) | [공통 sample](../../common/sample/README.ko.md) | [공통 E2E](../../common/e2e/README.ko.md) | [.NET](../../dotnet/README.ko.md) | [Java](../../java/README.ko.md) | [Node.js](../../node/README.ko.md) | [C++](../../cpp/README.ko.md)

# ZLink Framework Channel Topology

## 1. 목적

`ZLink Framework`는 zlink의 raw topology를 없애는 계층이 아니라, 그것을
**channel 단위 개념으로 다시 묶는 계층**이다.

즉 내부 구현은 `ROUTER`, `PUB`, `SUB`, `SPOT`, `STREAM`를 계속 쓸 수 있지만,
사용자에게는 아래 개념이 먼저 보여야 한다.

- channel name
- server
- client
- publisher
- subscriber
- stream session

## 2. channel grouping

현재 스펙은 provider grouping의 기준을 `channel name`으로 본다.

예를 들면 아래처럼 묶는다.

- `profile`
- `inventory`
- `payment`
- `game.stage.sync`

클라이언트는 endpoint 주소보다 `channel name`을 먼저 기준으로 삼는다.
현재 방향에서는 framework runtime도 이 기준을 그대로 따른다. 다만 channel은
"이름 하나 = 소켓 조합 하나"로 고정하지 않고, channel마다 어떤 역할을 열지
선언하는 구조를 기본으로 본다.

- `profile` channel에 `EnableServer()`를 두면 framework가 local
  `ROUTER(server)` ingress를 연다.
- `profile` channel에 `EnableClient()`를 두면 framework가 그 channel view에
  묶인 `DEALER(client)` outbound runtime을 만든다.
- `profile` channel에 `EnablePublisher()`를 두면 framework가 그 channel용
  publish 경로를 연다.
- `profile` channel에 `EnableSubscriber()`를 두면 framework가 그 channel용
  subscribe 경로를 연다.
- 같은 `channel name`에 속한 provider 집합은 location store 기반 자동 연결이
  갱신한다. provider는 자기 peer location row를 framework lifecycle로 store에
  등록하고, consumer는 그 row를 조회한 뒤 실제 연결 상태를 맞춘다. row 모델과 자동 연결
  규칙(role matching, pairwise initiator, 장애 중 마지막 연결 판단 유지)은
  [location runtime](40-location-runtime.ko.md) §2, §6을 따른다.

수동 연결을 쓰면 그 channel의 provider 집합을 직접 설정한다. 운영 점검이나 제어
전체 provider 집합을 읽을 수도 있다.

여기서 중요한 점은 연결 정책을 `channel` 전체가 아니라 **channel 안의 역할별
역할** 기준으로 봐야 한다는 점이다. 예를 들면 `profile.client`와
`profile.subscriber`는 서로 다른 연결 집합이다.

- `profile.client`는 request/send용 outbound `DEALER(client)` 연결을 뜻한다.
- `profile.subscriber`는 event subscribe용 `SUB` 연결을 뜻한다.

**한 역할의 peer 획득 방식은 하나로 확정된다.** 자동 연결이 관리하는 `DEALER`의 연결 집합은
자동 연결 상태 맞추기 작업이 소유하므로, 수동 `connect`와 섞이면 진실 공급원이 둘이 되기
때문이다. 실제 확정 규칙(수동 endpoint가 있으면 그 역할은 수동으로 확정되고 자동 reconcile이
돌지 않는다)은 §5가 소유한다. 따라서 framework는 `channel + capability`마다 연결 방식을 하나씩
고르고, 앱 전체에서는 역할별로 다른 방식을 나눠 쓰는 모델로 설명하는 편이 맞다.

이 구조가 일반적인 gateway 기반 호출 모델과 어떻게 다른지, 왜 gateway 없이도
location transparency를 얻을 수 있는지는 [01-overview.ko.md](../01-overview.ko.md)의
section 3을 참고한다.

## 3. 상호작용 모델과 topology 매핑

| 공용 모델 | 내부 기본 매핑 |
|-----------|---------------------|
| `request-response` | `DEALER(client) -> ROUTER(server)` channel 단위 요청 |
| `command` | `DEALER(client) -> ROUTER(server)` channel 단위 send |
| `publish-subscribe` | `PUB/SUB` 또는 같은 channel의 `SPOT` mesh |
| `stream` | `STREAM` |

위 표의 `request-response`, `command`, `publish-subscribe` 중 서버 간 framework
message로 흐르는 경로는 모두 multipart `header + payload` wire 계약을 따른다. routed
channel과 `SPOT` channel 위의 internal actor dispatch, internal session proxy도 같은
규칙을 사용한다.

반대로 `STREAM`은 하나의 stream packet을 주고받는 경로다. stream header와 payload는 그
단일 packet 안에서 frame으로 인코딩한다. 따라서 STREAM의 packet framing을 서버 간
multipart 계약으로 바꾸거나, 서버 간 multipart body를 stream frame처럼 단일 payload로
합치는 방식은 둘 다 이 topology 정책에 맞지 않는다.

RouteMesh는 일반 client-server channel과 socket 역할이 다르다. RouteMesh 구성원은
endpoint가 있든 없든 모두 `ROUTER` 역할을 사용한다. endpoint가 없는 `ROUTER`는
remote `ROUTER`를 항상 dial하고, 양쪽 모두 endpoint가 있으면 routing id에 따른
pairwise initiator 한쪽만 dial한다. RouteMesh에 `DEALER`를 등록하거나 과거 row를
위해 `ROUTER`와 `DEALER`를 함께 만드는 방식은 유효하지 않다.

SPOT topology는 "`AddSpotMesh(channelName)` 등록이 node 묶음의 active channel
view를 소유하는 모델"로 읽는 편이 맞다. 즉:

- `SpotNode`는 생성 시 channel 이름을 직접 소유하지 않는다.
- `AddSpotMesh(channelName)`가 node 묶음의 mesh 범위를
  정한다.
- 같은 `SpotNode`에는 active SPOT channel view를 하나만 둔다.
- 같은 channel의 다른 `SpotNode`와만 router / pub/sub mesh를 만든다.
- 다른 channel 호출은 attach된 `DEALER(client)` 경로로 푼다.
- local spot 인스턴스가 없는 외부 노드에서 특정 SPOT channel로 publish할 때는
  SpotNode publisher handle 경로를 따로 둔다.
- spot 타입 등록은 `AddSpotFactory<TSpot>()`처럼 타입 기반으로 두고,
  생성은 `CreateAsync<TSpot>(...)`처럼 그 타입으로 고른다.

SPOT 내부 peer topology와 channel 단위 호출은 서로 다른 경로로 설명한다.

### 3.1 요청 경로의 channel 선택 기준

요청을 보내는 쪽은 아래 기준으로 표면을 고른다. 샘플은 이 기준의 시연이므로
예외 없이 따른다.

1. **무상태 요청** — 어느 서버가 받아도 결과가 같은 요청은
   `ClientServerChannel`을 쓴다. client는 endpoint를 직접 지정하지 않고
   무인자 `EnableClient()`로 location store 발견에 맡긴다(§5.1).
2. **상태 대상 요청** — 특정 상태(actor, spot, 파티션)가 목적지인 요청은
   두 가지 중 하나를 쓴다.
   - **actor/spot 표면**: entry spot이 find/ensure/bind류 요청을 받도록 하고,
     보내는 쪽은 먼저 `SpotHandle`을 얻은 뒤 `RequestToSpot`으로 대상 spot에 보낸다.
     응답에 싣는 actor ref는 actor manager가 반환한 값을 그대로 쓴다.
   - **owner 일관 channel**: 소유자를 해시 등으로 정적으로 정하는 구조면,
     인스턴스별 channel 이름(예: `<name>.<instanceId>`)을 서버마다 서빙하고
     보내는 쪽이 owner 인스턴스의 channel로 보낸다. 생성(start)과 이후
     요청(continue)이 반드시 같은 owner로 가야 소유권이 갈라지지 않는다.
3. **RouteMesh channel은 샘플에서 쓰지 않는다** — node rid를 직접 지정하는
   route mesh 표면은 framework 내부(spot bridge 등)와 특수한 인프라 계층의
   것이다. 애플리케이션 수준 요청이 rid 지정이 필요해 보이면 대부분 2의
   상태 대상 요청이며, actor/spot 표면이나 owner 일관 channel로 표현한다.

## 4. 여러 서비스 channel을 호출하는 구성

한 서버가 여러 API channel에 요청을 보내는 구성에서도 사용자가 생각하는 단위는
socket이 아니라 channel client다.

예를 들면 아래처럼 보는 편이 자연스럽다.

- `profile` channel(client/server)은 `EnableServer()`와 `EnableClient()`를 가질 수 있다.
- `inventory` channel(client/server)은 `EnableClient()`만 가질 수 있다.
- `audit` channel(fanout)은 `EnablePublisher()`만 가질 수 있다.

**channel 종류는 배타적이다.** client/server channel에 publisher·subscriber 역할을 켤 수 없고,
fanout channel에 server·client 역할을 켤 수 없다. **한 이름의 channel이 두 종류를 겸할 수
없으므로**, 같은 서비스가 요청도 받고 event도 구독해야 하면 **서로 다른 channel 이름**으로
분리한다. 같은 이름을 두 번 등록하는 것도 설정 오류다
([channel 메시징 §4](11-channel-messaging.ko.md)).

outward API는 공용 client 하나로 보이더라도 내부 runtime은 channel마다
서로 다른 역할을 가질 수 있다. 현재 스펙은 이 channel별 역할 구조를
기본 방향으로 본다.

channel messaging의 일반 handler dispatch는 local `ROUTER(server)`가
받은 request 또는 command를 기준으로 설명한다. outbound
`DEALER(client)`가 받는 response는 먼저 보낸 request의 pending reply를 매칭하는
경로로 보고, 일반 handler dispatch 경로와 섞지 않는다.

### 4.1 handler namespace

handler dispatch key는 전역 packet 이름 하나로 보지 않는다. 같은 프로세스가
여러 channel에서 server 역할을 할 수 있으므로, 일반 channel messaging의
handler namespace는 inbound channel runtime 안에 둔다.

기본 dispatch key는 아래 조합이다.

- inbound `channel name`
- message kind (`Request`, `Command`, `Publish`). dispatch key 어휘에는 `send`를 포함하지 않는다. `Response`와 `Error`는 client측 reply correlation으로만 다루므로 dispatch key 집합에 두지 않는다.
- packet name

따라서 같은 애플리케이션 안에서도 `api` channel의 `AuthenticateReq`와 `admin`
channel의 `AuthenticateReq`는 서로 다른 handler로 매핑할 수 있어야 한다. 반대로
같은 handler type을 여러 channel에 다시 매핑하는 것은 허용한다. 이것은 code reuse
문제이고, dispatch namespace 문제와 다르다.

중복 검사는 실행 문맥 안에서만 수행한다. 일반 channel messaging에서는 같은
`channel name + kind + packet name`에 handler가 둘 이상이면 startup 오류다. 여기서
`kind`는 dispatch key 어휘인 `{Request, Command, Publish}` 중 하나다.
다른 channel에서 같은 `kind + packet name`을 다시 쓰는 것은 허용한다. routed
channel, actor, spot도 같은 원칙을 따르되, 각각의 `router channel`, Entry Spot
registry, user Spot registry를 namespace로 본다. actor 측 dispatch namespace의 정확한
모델 (`Entry Spot` / user Spot 별 actor packet 등록, `Configure()` 시점에 한 번 등록 등) 은
[22-actor-model.ko.md](22-actor-model.ko.md) §5에 정의한다.

handler attribute나 annotation은 packet kind와 packet name override를 표현한다.
channel name은 배포와 topology를 나타내는 값이므로 handler method attribute에
기본으로 넣지 않는다. 각 언어 binding은 framework 등록 단계에서 발견된 handler를
어떤 inbound channel에 노출할지 명시하는 API를 제공해야 한다.

**예외는 SPOT method attribute다.** spot node는 한 프로세스에 여러 개 있을 수 있고 method
단위 선언은 자기가 붙을 spot node를 스스로 지목해야 하므로, SPOT method attribute는 spot node
이름을 인자로 받는다. class 단위 SPOT handler attribute는 spot 타입이 실행 문맥을 정하므로
node 이름을 받지 않는다.

## 5. 자동 연결과 수동 연결

두 방식 모두 필요하다.

### 5.1 location store 기반 자동 연결

- 운영 환경 기본값으로 적합하다.
- channel name 기준 provider grouping과 자동 갱신에 유리하다. 연결 대상은
  location store의 peer row에서 resolve된다([location runtime](40-location-runtime.ko.md) §6).
- 각 channel이 자기 channel view를 독립적으로 유지하기에 적합하다.
- 일반 요청 경로에서는 다른 channel topology를 매번 조회하지 않고, 현재 channel
  view와 `rid` 집합만 보면 된다.

### 5.2 수동 연결

- 개발, 테스트, 단순 배포에서 유용하다.
- location store 없이도 같은 공용 API를 유지할 수 있다.
- 수동 연결은 `channel` 전체가 아니라 `channel + capability` 단위로 둔다.
- 같은 channel이라도 `client`, `subscriber`는 서로 다른 endpoint 집합을 가질 수
  있다.
- 수동 연결 역할은 startup 등록뿐 아니라 런타임 `connect`,
  `disconnect`, `list` 제어도 지원해야 한다.
- **peer 획득 방식은 역할 단위로 하나만 확정된다.** 같은 역할에 수동 endpoint가 하나라도
  있으면 그 역할은 수동 연결로 확정되고, location store를 함께 등록했더라도 그 역할의 자동
  연결 reconcile은 돌지 않는다. **startup 오류가 아니라 수동이 우선한다.** 자동 연결로 확정된
  역할에 런타임으로 수동 endpoint를 추가하려 하면 그때 거부된다.
- actor 모델은 location store 기반 자동 연결을 권장한다. session-bound actor와
  actor location resolve는 reconnect, scale-in/out, 위치 갱신을 dynamic하게
  다루므로 manual peer set과 잘 맞지 않는다. manual 연결은 single-peer 테스트나
  sample topology 검증에서만 actor 경로에 함께 쓴다.

`SPOT`도 같은 원칙으로 역할별 manual 연결을 나눠서 봐야 한다.

- `router` manual 연결은 **두 형태**를 갖는다. 보통은 endpoint 집합만 관리한다. 다만 **location
  store가 없는 구성**에서는 routed SPOT 메시지를 보낼 target `ROUTER`의 routing id를 해석할
  곳이 없으므로, **peer routing id를 endpoint와 함께 등록하는 형태**도 제공한다
  ([SPOT 메시징 §6](20-spot-messaging.ko.md)).
- route bridge channel socket manual 연결은 endpoint 집합만 관리한다.
- `pub/sub` manual 연결에서 등록하는 주소는 peer `SpotNode`의 mesh publish bind
  주소다. local `SUB/XSUB` 쪽이 그 주소로 붙는다.
- SpotNode publisher handle manual 연결도 endpoint 집합만 관리한다.
  다만 이 주소는 peer mesh 주소가 아니라 외부 publish ingress 주소다.

예를 들면 아래처럼 읽는 편이 맞다.

- `profile.client` 수동 연결
- `profile.subscriber` 수동 연결
- `audit.publisher`는 publish 경로만 연다
- `stage-node.router` 수동 연결
- `stage-node.pubsub` 수동 연결
- `stage-node.orders.client` 수동 연결
- `stage-node.game.stage.publisher` 수동 연결

수동 연결에서 framework가 관리하는 단위는 "channel 이름" 하나가 아니라
"channel 안의 특정 역할 runtime"이다.

### 5.3 location runtime query

- 운영 점검, warm-up, 관리 화면, 디버깅에 유용하다.
- 자동 연결이 지금 보고 있는 개별 channel view 밖의 전체 상태(원시 location row,
  runtime이 합성한 topology 보기, status)를 읽을 수 있다
  ([location runtime](40-location-runtime.ko.md) §7).
- 일반 요청 경로에서는 runtime query보다 channel view를 기준으로
  동작하는 편을 기본 방향으로 본다.

### 5.4 runtime monitoring source 이름

runtime monitoring은 raw socket 이름보다 logical source 이름을 먼저 보이는 편이
맞다. source 이름은 topology와 역할을 읽을 수 있게 잡아야 한다.

- socket event source는 `channel + capability` 또는 `spot node + capability`
  기준이 자연스럽다.
  예: `profile.server`, `profile.client`, `stage-node.router`
- location 계열 event source는 고정 이름을 쓴다([location runtime](40-location-runtime.ko.md) §9).
  예: `location-runtime`, `location-peer`
- spot event source는 `spot node` 등록 이름을 쓴다.
  예: `stage-node`

monitoring source 이름도 channel grouping과 역할 구분 원칙을 그대로 따른다.

공용 표면은 "channel별 역할 등록 방식"을 먼저 보이고, 내부 구현은
"channel별 역할 + 역할별 transport 매핑"을 기본으로 둔다.

### 5.5 서버 소켓의 최대 수신 메시지 크기

client/server channel과 route mesh channel의 서버 소켓 설정은 최대 수신 메시지 크기를
바이트 단위로 읽고 바꿀 수 있어야 한다. 양수 값은 실제 서버 소켓의 `MaxMessageSize`에
즉시 적용하며, `0`은 framework가 별도 상한을 지정하지 않는다는 뜻이다. 음수 값은 설정
오류로 거부한다.

상한은 수신하는 전체 transport 메시지에 적용한다. 애플리케이션 handler나 E2E 코드가
역직렬화한 payload 길이를 따로 검사해서 이 동작을 흉내 내면 안 된다. 상한을 넘긴 request는
응답을 만들 수 없으므로 호출자에게 기존 request timeout 오류로 끝나며, 해당 실패가 같은
channel의 이후 정상 크기 request를 막아서는 안 된다.

## 6. routing id 자동 할당

고정 routing id를 미리 정할 수 없는 배포에서는 client/server channel, fanout channel과 route mesh
channel builder가 location store에서 routing id 번호를 할당받을 수 있다. builder는 할당 가능한
번호 수와 선택적인 prefix를 선언한다. group 이름을 따로 지정하지 않으면 channel 이름을 group
이름과 prefix로 사용한다.

같은 allocation group의 모든 member는 slot 하나를 공유한다. 예를 들어 prefix가 `zone-`와
`route-`인 두 member가 slot 2를 공유하면 각각 `zone-2`, `route-2`를 사용한다. 번호는 1부터
시작하며 store는 유효한 owner가 없는 가장 작은 번호를 원자적으로 선택한다.

고정 routing id와 자동 할당은 같은 builder에서 함께 사용할 수 없다. group 이름만 지정하고 자동
할당을 설정하지 않은 경우도 설정 오류다. routing id는 socket을 만들거나 bind하기 전에 확정되어야
한다. fanout routing id는 목적지 선택에 사용하지 않고 해당 runtime의 추적·진단 identity로만
사용한다. 정확한 저장소와 lease 계약은 [location runtime](40-location-runtime.ko.md)을 따른다.

## 7. 범위 밖에 두는 것

- `DEALER <-> DEALER`를 **channel 등록 표면**에 공용 모델로 노출하는 일(location 계층에는
  `DealerMesh` 종류가 있으나 channel 등록 API가 그 값을 받지 않는다)
- `ROUTER -> DEALER` 임의 push를 channel messaging 공용 API로 노출하는 일
- raw multipart header를 application handler 인자로 직접 노출하는 일

이 둘은 필요해지면 고급 문서에서 다루되, 현재 `ZLink Framework` 공통 계약의 중심에는 두지
않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](../README.ko.md) | [이전: ZLink Framework API](../05-framework-api.ko.md) | [다음: Channel 메시징](11-channel-messaging.ko.md)
<!-- framework-adapter-nav:bottom:end -->

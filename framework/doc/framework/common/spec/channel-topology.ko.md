<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Message Model](message-model.ko.md) | [다음: ZLink Framework API](framework-api.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[문서 묶음](../README.ko.md) | [개요](overview.ko.md) | [use cases](../use-cases/README.ko.md) | [상호작용 모델](interaction-model.ko.md) | [메시지 모델](message-model.ko.md) | [framework API](framework-api.ko.md) | [검증](usecase-validation.ko.md) | [.NET](../../dotnet/README.ko.md) | [Java](../../java/README.ko.md) | [Node.js](../../node/README.ko.md) | [C++](../../cpp/README.ko.md)

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
- 같은 `channel name`에 속한 provider 집합은 discovery가 자동으로 갱신한다.

수동 연결을 쓰면 그 channel의 provider 집합을 직접 설정한다. 운영 점검이나 제어
plane에서는 Registry topology snapshot/query 또는 원격 `RegistryQueryClient`로
전체 provider 집합을 읽을 수도 있다.

여기서 중요한 점은 연결 정책을 `channel` 전체가 아니라 **channel 안의 역할별
역할** 기준으로 봐야 한다는 점이다. 예를 들면 `profile.client`와
`profile.subscriber`는 서로 다른 연결 집합이다.

- `profile.client`는 request/send용 outbound `DEALER(client)` 연결을 뜻한다.
- `profile.subscriber`는 event subscribe용 `SUB` 연결을 뜻한다.

같은 역할은 자동 연결과 수동 연결을 동시에 가질 수는 없다. zlink core에서
`Discovery`가 붙은 `DEALER`는 수동 `connect`를 허용하지 않기 때문이다.
따라서 framework는 `channel + capability`마다 연결 방식을 하나씩 고르고, 앱
전체에서는 역할별로 다른 방식을 나눠 쓰는 모델로 설명하는 편이 맞다.

이 구조가 일반적인 gateway 기반 호출 모델과 어떻게 다른지, 왜 gateway 없이도
location transparency를 얻을 수 있는지는 [overview.ko.md](overview.ko.md)의
section 3을 참고한다.

## 3. 상호작용 모델과 topology 매핑 초안

| 공용 모델 | 내부 기본 매핑 초안 |
|-----------|---------------------|
| `request-response` | `DEALER(client) -> ROUTER(server)` channel 단위 요청 |
| `command` | `DEALER(client) -> ROUTER(server)` channel 단위 send |
| `publish-subscribe` | `PUB/SUB` 또는 같은 channel의 `SPOT` mesh |
| `stream` | `STREAM` |
| `worker-dispatch` | 별도 조합 모델 |

위 표의 `request-response`, `command`, `publish-subscribe` 중 서버 간 framework
message로 흐르는 경로는 모두 multipart `header + payload` wire 계약을 따른다. routed
channel과 `SPOT` channel 위의 internal actor dispatch, internal session proxy도 같은
규칙을 사용한다.

반대로 `STREAM`은 하나의 stream packet을 주고받는 경로다. stream header와 payload는 그
단일 packet 안에서 frame으로 인코딩한다. 따라서 STREAM의 packet framing을 서버 간
multipart 계약으로 바꾸거나, 서버 간 multipart body를 stream frame처럼 단일 payload로
합치는 방식은 둘 다 이 topology 정책에 맞지 않는다.

현재 SPOT topology는 예전처럼 "하나의 `SpotNode`에 여러 channel surface를 붙이는
모델"보다, "`AddSpotMesh(channelName)` 등록이 node 묶음의 active channel
view를 소유하는 모델"로 읽는 편이 맞다. 즉:

- `SpotNode`는 생성 시 channel 이름을 직접 소유하지 않는다.
- `AddSpotMesh(channelName).UseDiscovery()`가 node 묶음의 mesh 범위를
  정한다.
- 같은 `SpotNode`에는 active SPOT channel view를 하나만 둔다.
- 같은 channel의 다른 `SpotNode`와만 router / pub/sub mesh를 만든다.
- 다른 channel 호출은 attach된 `DEALER(client)` 경로로 푼다.
- local spot 인스턴스가 없는 외부 노드에서 특정 SPOT channel로 publish할 때는
  attach된 spot publisher client 경로를 따로 둔다.
- spot 타입 등록은 `AddSpotFactory<TSpot>()`처럼 타입 기반으로 두고,
  생성은 `CreateAsync<TSpot>(...)`처럼 그 타입으로 고른다.

SPOT 내부 peer topology와 channel 단위 호출은 서로 다른 경로로 설명한다.

## 4. playhouse use case에 대한 해석

`playhouse` 시나리오에서는 play 서버가 여러 api channel에 요청을 보내야 할 수
있다. 이때 사용자가 생각하는 단위는 보통 socket이 아니라 channel client다.

예를 들면 아래처럼 보는 편이 자연스럽다.

- `profile` channel은 `EnableServer()`와 `EnableSubscriber()`를 같이 가질 수 있다.
- `inventory` channel은 `EnableClient()`만 가질 수 있다.
- `audit` channel은 `EnablePublisher()`만 가질 수 있다.

outward API는 공용 client 하나로 보이더라도 내부 runtime은 channel마다
역할별 역할을 가질 수 있다. 현재 스펙은 이 channel별 역할 구조를
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
- message kind (`request`, `command`, `event`). dispatch key 어휘에는 `send`를 포함하지 않는다. response는 client측 reply correlation으로만 다루므로 dispatch key 집합에 두지 않는다.
- packet name

따라서 같은 애플리케이션 안에서도 `api` channel의 `AuthenticateReq`와 `admin`
channel의 `AuthenticateReq`는 서로 다른 handler로 매핑할 수 있어야 한다. 반대로
같은 handler type을 여러 channel에 다시 매핑하는 것은 허용한다. 이것은 code reuse
문제이고, dispatch namespace 문제와 다르다.

중복 검사는 실행 문맥 안에서만 수행한다. 일반 channel messaging에서는 같은
`channel name + kind + packet name`에 handler가 둘 이상이면 startup 오류다. 여기서
`kind`는 dispatch key 어휘인 `{request, command, event}` 중 하나다.
다른 channel에서 같은 `kind + packet name`을 다시 쓰는 것은 허용한다. routed
channel, actor, spot도 같은 원칙을 따르되, 각각의 `router channel`, Entry Spot
registry, user Spot registry를 namespace로 본다. actor 측 dispatch namespace의 정확한
모델 (`Entry Spot` / user Spot 별 actor packet 등록, `Configure()` 시점에 한 번 등록 등) 은
[actor-model.ko.md](actor-model.ko.md) §5에 정의한다.

handler attribute나 annotation은 packet kind와 packet name override를 표현한다.
channel name은 배포와 topology를 나타내는 값이므로 handler method attribute에
기본으로 넣지 않는다. 각 언어 binding은 framework 등록 단계에서 발견된 handler를
어떤 inbound channel에 노출할지 명시하는 API를 제공해야 한다.

## 5. Discovery와 수동 연결

두 방식 모두 필요하다.

### 5.1 Discovery

- 운영 환경 기본값으로 적합하다.
- channel name 기준 provider grouping과 자동 갱신에 유리하다.
- 각 channel이 자기 channel view를 독립적으로 유지하기에 적합하다.
- 일반 요청 경로에서는 다른 channel topology를 매번 조회하지 않고, 현재 channel
  view와 `rid` 집합만 보면 된다.

### 5.2 수동 연결

- 개발, 테스트, 단순 배포에서 유용하다.
- Discovery 없이도 같은 공용 API를 유지할 수 있다.
- 수동 연결은 `channel` 전체가 아니라 `channel + capability` 단위로 둔다.
- 같은 channel이라도 `client`, `subscriber`는 서로 다른 endpoint 집합을 가질 수
  있다.
- 수동 연결 역할은 startup 등록뿐 아니라 런타임 `connect`,
  `disconnect`, `list` 제어도 지원해야 한다.
- 다만 같은 역할 안에서는 `Discovery`와 수동 연결을 섞지 않는다.
- actor 모델은 discovery 기반 자동 연결을 권장한다. session-bound actor와 actor
  route resolver는 reconnect, scale-in/out, 위치 갱신을 dynamic하게 다루므로
  manual peer set과 잘 맞지 않는다. manual 연결은 single-peer 테스트나 sample
  topology 검증에서만 actor 경로에 함께 쓴다.

`SPOT`도 같은 원칙으로 역할별 manual 연결을 나눠서 봐야 한다.

- `router` manual 연결도 endpoint 집합만 관리한다. 이 문서에서는 `connect()`
  호출 시 remote router id를 별도 파라미터로 받지 않는다.
- attach된 channel client manual 연결은 endpoint 집합만 관리한다.
- `pub/sub` manual 연결에서 등록하는 주소는 peer `SpotNode`의 mesh publish bind
  주소다. local `SUB/XSUB` 쪽이 그 주소로 붙는다.
- attach된 spot publisher client manual 연결도 endpoint 집합만 관리한다.
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

### 5.3 Registry topology query

- 운영 점검, warm-up, 관리 화면, 디버깅에 유용하다.
- Discovery가 지금 보고 있는 개별 channel view 밖의 전체 상태를 읽을 수 있다.
- 일반 요청 경로에서는 topology query보다 Discovery channel view를 기준으로
  동작하는 편을 기본 방향으로 본다.

### 5.4 runtime monitoring source 이름

runtime monitoring은 raw socket 이름보다 logical source 이름을 먼저 보이는 편이
맞다. source 이름은 topology와 역할을 읽을 수 있게 잡아야 한다.

- socket event source는 `channel + capability` 또는 `spot node + capability`
  기준이 자연스럽다.
  예: `profile.server`, `profile.client`, `stage-node.router`
- discovery event source는 logical discovery registration 이름을 쓰는 편이 맞다.
  예: `profile.client.discovery`, `game.stage.discovery`
- registry event source는 infrastructure 단위를 드러내는 이름을 쓴다.
  예: `registry`
- spot event source는 `spot node` 등록 이름을 쓴다.
  예: `stage-node`

monitoring source 이름도 channel grouping과 역할 구분 원칙을 그대로 따른다.

공용 표면은 "channel별 역할 등록 방식"을 먼저 보이고, 내부 구현은
"channel별 역할 + 역할별 transport 매핑"을 기본으로 둔다.

## 6. 범위 밖에 두는 것

- `DEALER <-> DEALER`를 공용 모델로 노출하는 일
- `ROUTER -> DEALER` 임의 push를 channel messaging 공용 API로 노출하는 일
- raw multipart header를 application handler 인자로 직접 노출하는 일

이 둘은 필요해지면 고급 문서에서 다루되, 현재 `ZLink Framework` 초안의 중심에는 두지
않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Message Model](message-model.ko.md) | [다음: ZLink Framework API](framework-api.ko.md)
<!-- framework-adapter-nav:bottom:end -->

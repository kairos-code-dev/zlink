[← 목차](README.ko.md)

# 3. 핵심 개념

ZLink framework 는 **여섯 가지 핵심 개념**으로 선다:
**RouteMesh · ChannelName · Spot · Actor · STREAM · location store**. 나머지 챕터는 전부 이
여섯 개념의 조합이다. 낯선 단어가 나오면 먼저 §0 용어 표에서 한 줄로 잡고, §1~§5 에서
각 개념을 차례로 본다. §6 은 이들을 받치는 실행·구성 모델(app 수명주기, DI,
핸들러·실행 모델)이다.

## 0. 용어 빠르게 잡기

가이드에 자주 나오는 용어를 먼저 잡아 둔다. 정식 계약은
[공통 스펙 목차](../../common/README.ko.md)와
[C++ framework spec](../../spec/server/languages/cpp/interfaces/README.ko.md)이 다룬다.

| 용어 | 한 줄 풀이 |
|------|-----------|
| **channel(채널)** | 서버 간 호출을 묶는 논리 이름. `host:port` 대신 `"orders"` 같은 이름으로 부른다 |
| **ChannelName membership** | MeshNode가 제공하는 논리 메시지 처리 범위 |
| **handler(핸들러)** | 들어온 메시지를 처리하는 클래스나 SPOT 메서드 |
| **client** | 다른 서비스로 호출을 보내는 주입 객체(예: `request_client_t`) |
| **request / send / publish** | 각각 응답 받는 호출 / 응답 없는 단방향 통지 / 여러 구독자에게 발행 |
| **packet name(패킷 이름)** | 같은 channel 안에서 어느 메시지 종류인지 구분하는 키 |
| **codec** | payload를 바이트로 직렬화·역직렬화하는 방식 |
| **SPOT** | room/zone처럼 동적으로 생겼다 사라지는 상태 노드. 한 SPOT의 callback은 직렬 실행된다 |
| **actor** | 외부 client나 사용자 하나를 대표하는 서버 쪽 객체 |
| **Entry Spot** | actor가 생성 직후 머무는 기본 실행 위치 |
| **STREAM(스트림)** | 외부 client와의 연결 지향 양방향 채널 |
| **session(세션)** | STREAM 연결 하나에 대응하는 서버 측 객체 |
| **location store** | MeshNode·Spot·Actor 위치와 revision을 저장하는 공유 저장소 |
| **peer intent** | location descriptor 또는 manual 설정으로 유지하는 MeshNode 연결 의도 |
| **RoutingId** | 노드·SPOT의 논리 주소 |
| **correlation(상관)** | 요청과 응답을 짝지어 주는 식별 정보. framework가 자동 처리 |
| **deadline / timeout** | 응답을 얼마나 기다릴지의 상한 시간 |
| **DI / lifecycle** | 의존성 주입과 앱 시작·종료 수명 관리 |

## 0.1 Payload 직렬화

C++ framework의 일반 payload는 메시지 타입마다 codec을 설정하지 않는다.
payload 타입이 `to_json`/`from_json`을 제공하면 framework가 기본 JSON 직렬화 경로를
사용한다. 사용자는 handler의 `request_type`, `reply_type`, `message_type`,
`event_type`을 선언하고 handler group에 등록하면 된다.

`options.codecs().use(...)`는 일반 메시지 타입을 나열하는 API가 아니다. 기본 JSON으로
표현할 수 없는 payload나 별도 binary serializer가 필요한 extension을 연결할 때만 쓴다.

`zlink::framework::message_t`에 이미 타입이 지워진 payload를 담아 전달하는 고급 경로는
runtime이 타입 이름만 보고 serializer를 찾아야 한다. 이 경우에는 handler 등록으로 알려진
타입이거나 custom serializer extension이 제공한 타입이어야 한다. 일반 업무 코드는 가능한 한
typed request/send/publish API를 사용하고, raw frame이나 `message_t` 우회로 payload 변환을
직접 맡지 않는다.

## 1. RouteMesh와 ChannelName — 서버 간 연결

RouteMesh는 MeshName으로 식별하는 물리 node 집합이고 ChannelName은 그 안의 논리
membership이다. Application은 `request_client_t`에 MeshName과 ChannelName을 주며,
framework는 ready positive-weight member 하나를 선택한다. 특정 MeshNode가 대상이면
`route_client_t`에 target RID를 준다.

| 목적 | 등록과 호출 |
|------|-------------|
| ChannelName select-one | `add_route_mesh(mesh)` + `channel_name(channel)`, `request(mesh, channel, ...)` |
| Node direct | 같은 MeshNode 등록, `request_to_node(mesh, target_rid, ...)` |
| Logical Multicast | Spot context의 `publish(channel, topic, event)` |
| classic fanout | 독립 `add_fanout_channel(name)` |

MeshNode의 수신 endpoint와 RID는 `listen(...)`, `set_routing_id(...)`로 정한다. Manual
peer는 `peer_connections().connect(...)`, 자동 peer는 location store가 소유한다.
ChannelName과 handler group은 서로 다르다. Group은 코드의 handler 묶음이고
ChannelName은 MeshName 안의 배포 membership이다. 사용법은
[7장 채널 메시징](07-channel-messaging.ko.md)이 다룬다. Transport 배선은 guide의 공개
사용법이 아니라 [runtime architecture](../internals/runtime-architecture.ko.md)가 다룬다.

> **주의:** channel 이름과 handler **group 이름**은 서로 다르다. group 은 코드 안
> 논리 묶음(`"api"`)이고, channel 은 배포 식별자(`"tictactoe.api"`)다. 같은 group 을
> 여러 channel 에 매핑할 수 있다.

## 2. spot — 상태 단위

Spot은 room/zone/stage처럼 동적으로 생성하는 상태 owner다. 같은 Spot의 packet,
lifecycle과 timer callback은 Spot application queue에서 직렬 실행된다. Actor payload는
Spot registry가 아니라 각 Actor의 handler registry에 등록하며 Actor queue에서 실행된다.

한 SPOT 에 들어오는 모든 일은 **단일 큐**를 통과해 한 줄로 처리된다 — 그래서 상태에
lock 이 없다.

```mermaid
graph LR
    M1["packet"] --> Q["단일 큐<br/>직렬 실행"]
    M2["timer"] --> Q
    M3["Actor lifecycle"] --> Q
    Q --> ST["SPOT 상태<br/>(lock 불필요)"]
```

직렬 실행이 보장되는 이유와 작성법은 [8장 SPOT](08-spot.ko.md).

## 3. actor — ID 로 식별되는 상태 객체

actor 는 **외부 client 나 사용자 하나를 대표하는 서버 쪽 상태 객체**다. 같은 actor
id 로 온 메시지는 늘 같은 인스턴스가 처리하고, 외부 client session 을 actor 에
바인딩해 **연결 서버(세션)와 로직 서버(actor)를 분리**할 수 있다.

```mermaid
graph LR
    S1["msg · id=42"] --> RT{"actor id<br/>라우팅"}
    S2["msg · id=42"] --> RT
    S3["msg · id=7"] --> RT
    RT -->|id=42| A42["actor 42<br/>(같은 인스턴스)"]
    RT -->|id=7| A7["actor 7"]
```

상세는 [9장 Actor · Session](09-actor-session.ko.md).

## 4. stream — 외부 client 연결

stream 은 모바일·게임 같은 **외부 client 와의 연결 지향 양방향 채널**이다. 서버
간 channel(§1)과 달리 연결 하나가 서버 측 **session** 객체에 대응한다. 서버 Framework는
연결 수명과 packet dispatch를 관리하고, 재연결·heartbeat는 client connector가 관리한다.

```mermaid
graph LR
    C["모바일·게임<br/>client connector"] <-->|"STREAM 연결"| SV["STREAM 서버"]
    SV --- SE["session<br/>(연결 1개 = 객체 1개)"]
```

상세는 [10장 Stream](10-stream.ko.md).

## 5. Location store와 manual peer

Production 자동 연결은 Redis location store의 MeshNode descriptor를 사용한다. 각
MeshNode가 MeshName, RID, endpoint와 ChannelName membership을 기록하면 같은 MeshName의
다른 node가 revision을 읽어 peer intent를 갱신한다.

```mermaid
graph LR
    A["MeshNode A"] -->|"descriptor upsert"| S["Redis location store"]
    B["MeshNode B"] -->|"revision 조회"| S
    S -.->|"peer descriptor"| B
    B -->|"peer admission"| A
```

고정된 개발 topology에서는 `mesh.peer_connections().connect(endpoint)`로 manual peer를
등록할 수 있다. 자동 descriptor와 manual intent는 같은 MeshName·RID·security admission
검증을 거친다. 자세한 등록과 운영은 [11장 location store](11-registry.ko.md)가 다룬다.

## 6. 보조 — 실행·구성 모델

위 다섯 개념을 받치는 공통 동작이다. 여기서 한 번 짚고, 상세는 각 챕터가 소유한다.

### 6.1 핸들러 모델 — 노드 핸들러 vs SPOT 핸들러

핸들러는 실행 컨텍스트에 따라 두 종류로 나뉘고, 구조와 수명이 완전히 다르다.

- **노드 핸들러(채널·HTTP)** — 독립 클래스. `request_type` / `reply_type` /
  `topic_name` 멤버가 계약이고, `dependency_types` + 생성자 주입으로 의존성을 받는다.
  수명은 **transient**(요청마다 새로), 실행은 **동시**(worker 풀). 그래서 가변
  도메인 상태를 핸들러 멤버에 두지 않는다.
- **Spot 핸들러** — `spot_t` 또는 `entry_spot_t`를 상속하고 `configure()`에서
  `add_handler<&T::method>()`나 `add_subscribe<&T::method>()`로 등록한다. Actor payload는
  Actor의 `actor_context_t::handlers()`에 등록한다.

| | 노드 핸들러 (채널·HTTP) | entry spot | room spot |
|---|---|---|---|
| 기반 | 독립 클래스 | `entry_spot_t` 상속 | `spot_t` 상속 |
| 수명 | transient (요청마다) | 노드와 동일 (영속) | 상태 단위와 동일 (영속) |
| 실행 | 동시 (worker pool) | lifecycle은 Entry Spot queue, Actor payload는 각 Actor queue | Spot application queue에서 직렬 |
| 공유 상태 | 핸들러에 두지 않음 | Entry lifecycle 상태와 Actor별 상태의 owner를 구분 | 같은 Spot turn에서 안전 |
| 역할 | 요청 처리·위임 | 배정·매칭·할당 | 도메인 상태 소유·처리 |
| 계약 | `request_type`/`reply_type`/`topic_name` | lifecycle member + Actor handler registry | `configure()` + Spot handler registry + lifecycle member |
| outbound | DI의 `request_client_t` / `route_client_t` | owner MeshNode context | owner MeshNode context |

**실행 모델 비교** — 같은 3개 요청이 두 핸들러에서 어떻게 도는가:

```mermaid
graph TB
    subgraph N ["노드 핸들러 — 동시 (worker 풀)"]
        direction LR
        NR1["req A"] --> NW1["worker 1 ▶ 처리"]
        NR2["req B"] --> NW2["worker 2 ▶ 처리"]
        NR3["req C"] --> NW3["worker 3 ▶ 처리"]
    end
    subgraph S ["SPOT 핸들러 — 직렬 (단일 큐)"]
        direction LR
        SR1["req A"] --> SQ["단일 큐"]
        SR2["req B"] --> SQ
        SR3["req C"] --> SQ
        SQ --> SEX["A → B → C<br/>하나씩 순서대로"]
    end
```

노드 핸들러는 요청마다 다른 worker 가 **동시에** 처리하니 핸들러에 가변 상태를 두면
경합이 난다. SPOT 핸들러는 단일 큐로 **한 번에 하나씩** 처리하니 상태에 lock 이
필요 없다.

가변 도메인 상태(게임 룸 등)는 **SPOT**, 불변 구성(topology)은 싱글톤 서비스, 공유
인프라(캐시·카운터)는 싱글톤 + 자체 동기화에 둔다. SPOT 핸들러 작성과 직렬 실행
보장은 [8장](08-spot.ko.md), 채널 핸들러 노출은 [7장](07-channel-messaging.ko.md).

**handler 노출은 명시적이다** — `options.handlers().group("api").add<T>()` 로 group 에 넣고,
channel 등록에서 `use_handler_group("api")` 로 붙인다. 시작 단계에서 같은 handler
group 안 packet 중복, registry handler 중복, client/subscriber 의 연결 경로 누락,
허용되지 않는 반환형 등이 거부된다.

### 6.2 실행 모델 — `task_t` / `result_t`, `co_await`

프레임워크 전반의 비동기 값은 `task_t<T>`, 성공/실패는 `result_t<T>` 로 표현된다.
규칙은 하나다 — **런타임(핸들러) 스레드에서는 `co_await`, blocking(`.result()`)은
테스트·클라이언트 시나리오에서만.** 실패는 `co_await` 경로에서
`framework_exception_t`(`kind()`/`is_retriable()`)로 던져지고, `result_t` 경로에서는
`error()` 로 조회한다.

```cpp
zlink::framework::task_t<create_game_http_res_t>
handle (const create_game_http_req_t &request)
{
    auto room = co_await _client
                  .request ("tictactoe.application", "tictactoe.play",
                            create_game_req_t{request.game_name})
                  .async<create_game_res_t> ();
    co_return create_game_http_res_t{room.room_id,
                                     room.game_name,
                                     room.owner_play_endpoint,
                                     room.play_endpoints,
                                     room.play_nodes,
                                     room.required_level};
}
```

채널·HTTP 핸들러는 **worker 풀**에서 실행된다. `options.configure_worker()`가 반환하는
설정의 `min_threads`, `max_threads`, `idle_timeout`, `max_queue_length`로 worker 풀을
구성한다. 핸들러가 `co_await` 에 도달하면
코루틴만 멈추고(suspend) 실행 스레드는 다른 큐 항목을 처리한다. 같은 Spot 큐는 그
handler 완료 전까지 다음 callback 을 시작하지 않는다.

핵심은 **이벤트마다 코루틴 하나, 스레드는 공유**다 — SPOT 의 event(message·timer)는
각각 `task_t` 코루틴이 되어 소수의 worker 스레드에 다중화되고, `co_await` 에 걸린
코루틴은 스레드를 **놓는다**(blocking 아님). 그래서 스레드 몇 개로 대기 중인 코루틴
수천 개를 떠받친다.

```mermaid
graph LR
    subgraph EV ["SPOT event 마다 코루틴 하나"]
        E1["message A"]
        E2["timer tick"]
        E3["message B"]
    end
    E1 --> T1["task_t 코루틴 A"]
    E2 --> T2["task_t 코루틴 T"]
    E3 --> T3["task_t 코루틴 B"]
    T1 --> POOL["worker 스레드 풀<br/>(소수, CPU 코어 수)"]
    T2 --> POOL
    T3 --> POOL
    POOL -.->|"co_await 도달 → suspend"| WAIT["대기 중 코루틴<br/>(스레드 점유 0)"]
    WAIT -.->|"응답 도착 → resume"| POOL
```

아래 타임라인은 같은 흐름을 시간순으로 본 것이다 — A 가 `co_await` 로 suspend 되면
같은 스레드가 즉시 B 를 처리하고, A 는 응답이 오면 resume 된다.

```mermaid
sequenceDiagram
    participant W as worker 스레드
    participant H1 as 핸들러 A (코루틴)
    participant CH as Play 채널
    participant H2 as 핸들러 B (코루틴)

    W->>H1: handle() 실행
    activate H1
    H1->>CH: co_await request(...).async()
    deactivate H1
    Note over H1: suspend — 응답 대기 (스레드 점유 없음)
    Note over W: 워커는 즉시 다음 일로
    W->>H2: handle() 실행
    activate H2
    H2-->>W: co_return (완료)
    deactivate H2
    CH-->>H1: 응답 도착 → resume
    activate H1
    H1-->>W: co_return (완료)
    deactivate H1
```

그래서 비동기 호출을 콜백 없이 **동기식 코드처럼 위에서 아래로** 쓰면서도, worker
몇 개로 수많은 동시 요청을 처리한다. 같은 코드를 `.result()` 로 쓰면 스레드 하나가
통째로 잠들기 때문에 핸들러 안에서 금지한다.

### 6.3 app_t 수명주기

`app_t` 는 구성 → 서비스 → 종료의 호스트 수명주기를 소유한다. `run(argc, argv)` 는
블로킹이고 반환값이 종료 코드다.

```mermaid
stateDiagram-v2
    direction LR
    state "구성 단계" as configure
    state "서비스 중" as serving
    state "종료" as stopping
    [*] --> configure: create()
    configure: config / logging
    configure: add_zlink_framework
    configure: add_hosted_service
    configure --> serving: run(argc, argv)
    serving: 채널·HTTP·spot dispatch
    serving --> stopping: stop() / request_stop() / 신호
    stopping: hosted service stop → 채널·HTTP 정리
    stopping --> [*]: 종료 코드 반환
```

- **구성 단계** — `run` 전에 모든 선언을 끝낸다. 잘못된 구성은 구성 시점이나 `run`
  시작에서 예외로 거부된다.
- **종료** — `request_stop()` 은 비동기 요청(신호 핸들러 등), `stop()` 은 동기 정지.
  종료 시 **등록된 hosted service 를 역순으로 `stop()`** 한다(채널·stream·HTTP 도
  hosted service 로 편입돼 같은 경로로 정리된다).
- 백그라운드 작업은 `hosted_service_t` 로 수명주기에 편입시킨다.

### 6.4 구성: DI 컨테이너 · 진입점 · module_t

- **DI 컨테이너** — `options.services()` 에 `add_singleton/scoped/transient` 로
  등록하고, 소비 측은 `dependency_types` + 생성자 주입(또는 `get_required<T>()`)으로
  받는다. 전체 API 는 [4장 DI 컨테이너](04-di-container.ko.md).
- **구성 표면 지도** — `app_t` 진입점이 역할별로 나뉜다:

  | 진입점 | 역할 | 다루는 장 |
  |--------|------|-----------|
  | `app.config()` / `app.logging()` | 설정·로그 | [5장](05-configuration.ko.md) · 12장 |
  | `app.monitoring()` / `app.metrics()` / `app.health()` | 관측·상태 | 12장 |
  | `app.add_zlink_framework(람다)` | **zlink 토폴로지 선언** (채널/SPOT/stream/registry) | 6~11장 |
  | `app.add_module(...)` / `add_zlink_framework<TModule>()` | 구성 패키징 | 아래 |
  | `app.advanced()` | services/handlers/zlink builder 직접 접근 (탈출구) | — |

- **module_t** — 기능 단위 구성(서비스·zlink 토폴로지·핸들러)을 한 단위로 묶어
  재사용한다. `configure_services / configure_zlink / configure_handlers /
  configure_monitoring` 을 구현하고 `app.add_zlink_framework<TModule>()` 로 붙인다.

[다음: DI 컨테이너 →](04-di-container.ko.md)

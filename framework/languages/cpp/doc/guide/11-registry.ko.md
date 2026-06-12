[← 목차](./README.ko.md)

# 11. Registry

## 1. registry가 하는 일

registry는 **노드들이 서로의 주소를 찾는 이름 서비스**다. registry가 없으면
모든 채널 client에 상대 endpoint를 직접 적어야 한다. registry를 두면 서버는
자기 endpoint를 등록(publish)하고, 클라이언트는 채널 이름만으로 상대를 찾는다.

registry가 없을 때와 있을 때 — 토폴로지가 그물에서 별형으로 바뀐다.

```mermaid
flowchart LR
    subgraph before["registry 없음 — endpoint를 서로 하드코딩"]
        A1["Api"]:::infra -- "tcp://10.30.1.15:5561" --> P1["Play"]:::infra
        A1 -- "tcp://10.30.1.16:5562" --> S1["Session"]:::infra
        S1 -- "tcp://10.30.1.15:5561" --> P1
    end
    subgraph after["registry 있음 — 채널 이름만 알면 된다"]
        R["Registry"]:::infra
        A2["Api"]:::channel -.->|"play 어디?"| R
        S2["Session"]:::channel -.->|"play 어디?"| R
        P2["Play"]:::channel -.->|등록| R
        A2 == 연결 ==> P2
        S2 == 연결 ==> P2
    end

    classDef channel fill:#e3f2fd,stroke:#1565c0,color:#000000
    classDef infra fill:#eceff1,stroke:#546e7a,color:#000000
```

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'signalTextColor': '#000000', 'actorTextColor': '#000000', 'noteTextColor': '#000000', 'actorBkg': '#ffffff', 'actorBorder': '#555555', 'activationBorderColor': '#555555'}}}%%
sequenceDiagram
    participant P as Play 서버
    participant R as Registry
    participant A as Api 서버

    P->>R: enable_server → "play 채널 = tcp://10.30.1.15:5561" 등록 (ROUTER)
    A->>R: enable_client() → "play 채널 어디?" 질의
    R-->>A: tcp://10.30.1.15:5561
    A->>P: 채널 연결 + request
    Note over R,A: 변경 통지는 PUB endpoint로 흘러 자동 갱신
```

## 2. registry 서버 띄우기

registry는 전용 프로세스로 띄우는 것이 기본이다. 선언은 한 줄이다.

```cpp
app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
    options.enable_registry (topology.registry_pub_endpoint,       // 변경 통지(pub)
                             topology.registry_router_endpoint);   // 질의/등록(router)
});
```

## 3. 소비자: discovery 연동

registry를 쓰는 모든 프로세스는 discovery에 registry endpoint를 알려 준다.

```cpp
options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
```

그 다음부터 endpoint 생략 형태가 동작한다.

```cpp
// 서버 쪽 — enable_server는 그대로. 등록은 discovery가 처리
options.add_client_server_channel ("bingo.play")
  .enable_server (topology.play_channel_endpoint)
  .use_handler_group ("play");

// 클라이언트 쪽 — endpoint 없이 채널 이름만으로 연결
options.add_client_server_channel ("bingo.api").enable_client ();
```

`enable_client()`(무인자)가 discovery 모드다. endpoint를 직접 주는
`enable_client("tcp://...")`와 같은 채널에서 혼용하지 않는다.

## 4. SPOT과 discovery

spot mesh는 discovery 채널 이름으로 노드들을 발견한다([8장 §2](./08-spot.ko.md)).

```cpp
options.add_spot_mesh ("bingo.room.discovery")    // 이 이름이 discovery 채널
  .add_node ("bingo.room.node")
  .enable_router (topology.play_spot_router_endpoint, topology.play_rid)
  .enable_pub_sub (topology.play_spot_endpoint)
  // ...
```

spot의 원격 주소를 registry에서 받도록 하려면:

```cpp
options.use_registry_spot_remote_addresses ();
// 또는 라우트 채널 지정
options.use_registry_spot_remote_addresses ("bingo.room.routes");
```

## 5. 점검

- registry 상태 check를 health에 올릴 수 있다 —
  [12장 §3](./12-monitoring.ko.md)의 `add_registry_check`.
- registry 이벤트(등록/해제/조회) 관측은 `add_registry_events` —
  [12장 §2](./12-monitoring.ko.md).
- 동작 예제: `samples/Bingo`가 registry 포함 4-서버 토폴로지의 정본이다
  ([14장](./14-samples-map.ko.md)).

[다음: Monitoring →](./12-monitoring.ko.md)

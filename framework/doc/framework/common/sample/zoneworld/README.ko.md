# ZoneWorld Sample Scenario

[샘플 목록](../README.ko.md)

이 문서는 ZoneWorld의 언어 중립 시나리오와 검증 기준을 정의한다. ZoneWorld는 .NET과 Node.js
framework sample로 제공하며, 두 server 구현은 같은 message 계약과 self-check를 사용한다. 브라우저
UI는 TypeScript client 하나를 공유한다. Wire 계약이 언어 중립이므로 같은 client가 두 server에
연결한다. 현재 두 언어 디렉터리는 server, headless scenario client와 `run_sample`
스크립트를 포함하며, 각 runner가 해당 언어 server의 전체 self-check를 실행한다.

ZoneNode는 §3.2의 prefix로 transport RID를 자동 발급받아야 한다. 고정 `NodeRid`를
설정하면 시작 순서와 process 교체에 따라 달라지는 transport identity를 업무 identity로
오해할 수 있다.

Server와 headless 시나리오 client는 지원 언어의 sample 디렉터리에 함께 둔다. 여러 언어가 공유하는
브라우저 client만 `shared_sample`에 둔다.

```text
framework/languages/dotnet/samples/ZoneWorld/
  Shared/     Language-neutral message contracts
  Server/     Gateway, ZoneNode, and Ops roles
  Client/     Headless ZW-* scenarios
  run_sample.sh

framework/languages/node/samples/ZoneWorld/
  Shared/     Language-neutral message contracts
  Server/     Gateway, ZoneNode, and Ops roles
  Client/     Headless ZW-* scenarios
  run_sample.sh

framework/languages/shared_sample/zoneworld/
  client/     Shared TypeScript browser client
```

Headless 시나리오 client는 자기 언어의 server 동작을 검증하므로 언어별 디렉터리에 둔다. 브라우저
client는 .NET과 Node.js server가 공유하므로 `shared_sample`에 둔다. 이 구분으로 언어별 검증 코드와
공유 UI의 책임이 섞이지 않는다.

두 `Client`는 서로 다른 계약이다. `<lang>/samples/ZoneWorld/Client/`는 브라우저 없이 §11의 `ZW-*`
시나리오를 실행한다. `shared_sample`의 브라우저 client는 §10의 화면과 실제 browser transport를
검증한다. §6은 언어별 `Server/` 구조를, §9.3은 공유 browser client 구조를 정의한다.

## 1. 목적

ZoneWorld는 **zone 분할 MMORPG**와 그것을 **운영·관제하는 콘솔**을 한 샘플에 담는다.
`01. Overview` 장 §2가 게임 서버 4갈래 중 ①로
소개하는 **zoning**(월드를 지리 구역으로 나누고 각 구역을 전역 `SpotId`로 식별하는 방식)을
보여 주는 첫 샘플이다. 어느 물리 노드가 구역을 담당하는지는 Location Store와 framework가
결정하며 application 설정에 고정하지 않는다.

ZoneWorld는 multi-node 게임에서 노드 등록 상태 관찰, 전 노드 공지와 특정 노드 점검을 각각 어떤
공개 표면으로 표현하는지 함께 보여 준다.

이 샘플이 보여 주는 것:

- 플레이어가 경계를 넘으면 actor가 인접 zone Spot에 join한다. owner가 다르면 framework가
  relocation하며 client 연결은 유지된다.
- 경계 근처 상태를 **인접 zone에 Logical Multicast으로 동기화**한다.
- 관제 콘솔이 **runtime event로 노드 등록·연결 상태를 관찰**한다.
- 관제 콘솔이 **channel fanout으로 전 노드에 공지**한다. 발행자는 노드 목록을 갖지 않는다.
- 관제 콘솔이 desired state를 저장하고 **fanout으로 전 노드에 점검 변경을 알린다**.
  각 노드는 자기 `NodeId`에 해당하는 상태를 적용한다.
- **브라우저에서 확인한다.** 게임 화면에서 경계 이동을, 관제 화면에서 노드 상태와
  점검 모드 전환을 확인한다.

### 1.1 표면 선택 기준

이 샘플의 교육 목표다. "여러 노드에 무언가를 한다"가 상황마다 다른 표면을 요구한다.
선택 기준은 [channel topology spec §2·5](../../spec/07-channel-topology.ko.md)을 따른다.

| 하려는 일 | 쓰는 것 | 다른 것으로 안 되는 이유 |
|---|---|---|
| 어느 노드가 등록·연결됐는지 확인한다 | **runtime event** | 요청이 아니라 **변화 알림**이다. 노드가 종료되면 요청할 대상이 없다 |
| **전 노드**에 공지를 전달한다 | **channel fanout** | 발행자가 노드 목록을 갖지 않는다. `SendToChannel`이면 발행자가 노드 목록을 관리해야 한다 |
| **특정 노드**를 점검 모드로 전환한다 | **desired state + channel fanout** | `NodeId`는 application 식별자다. Transport RID를 application 주소로 사용하지 않는다 |
| **한 zone의 모든 플레이어**에게 전달한다 | zone spot → 그 spot의 actor들 → 각 bound session | zone spot은 lifecycle에서 받은 `PlayerId` 목록만 보관한다. Framework가 각 `ActorId`의 현재 위치를 찾아 전달한다 |
| **특정 플레이어 한 명**에게 전달한다 | 그 player actor → 자기 bound session | actor binding이 연결 위치를 이미 해결하므로, 발행자가 노드·연결을 지정하지 않는다 |

> ZoneWorld의 ChannelName, Spot·Actor와 Logical Multicast는 하나의 MeshNode ROUTER를
> 사용한다. 공지와 점검 변경은 별도 classic fanout channel로 전달한다.

## 2. 월드 규격

지원 언어가 같은 결과를 내려면 규칙이 고정되어야 한다.

| 항목 | 값 |
|------|-----|
| 좌표 | `0 <= X < 100`, `0 <= Y < 100` (정수) |
| zone 분할 | 4개 사분면 |
| `zone-nw` | `X < 50`, `Y < 50` |
| `zone-ne` | `X >= 50`, `Y < 50` |
| `zone-sw` | `X < 50`, `Y >= 50` |
| `zone-se` | `X >= 50`, `Y >= 50` |
| zone 배치 | 네 `ZoneId`를 global `SpotId`로 사용한다. owner node는 framework가 선택한다 |
| 인접 | **변을 공유하는 zone만 인접**이다. 대각선(`zone-nw`↔`zone-se`)은 인접이 아니다 |
| 경계 밴드 | 경계에서 **10 이내**. 이 안의 플레이어는 그 경계를 공유하는 인접 zone에도 보인다 |
| tick | **100ms**. zone spot 생성 시 `Tick = 0`이고 tick마다 1씩 증가한다 |
| 이동 제한 | 한 `MoveMsg`의 이동 거리는 각 축 **최대 5** |
| 입장 좌표 | 서버가 정한다. 항상 `(25, 25)` = `zone-nw` |

### 2.1 좌표의 소유자

**player actor가 좌표의 권위 소유자다.** zone spot은 렌더·경계 동기화를 위해 그 값의
사본을 보관한다. 이 방향을 고정해야 지원 언어가 같은 결과를 낸다.

| 주체 | 소유하는 것 |
|---|---|
| player actor | `X`, `Y`, 현재 `ZoneId` — **권위** |
| zone spot | `PlayerId → (X, Y, IsBot)` map — player actor가 보낸 값과 lifecycle에서 확인한 membership의 사본(§8.3) |

이동 처리 순서:

1. player actor가 `MoveMsg`를 받아 §2.2로 검증한다.
2. 거부면 `MoveRejectedNotify`를 push하고 끝낸다(좌표 불변).
3. 승인이면 좌표를 갱신하고, zone 변경 여부에 따라 갈린다.
   - **zone 불변**: actor가 현재 zone의 global `SpotId`로 `UpdatePositionMsg`를 보내 좌표 사본을 갱신한다.
     actor handler와 Spot은 서로 다른 application turn이므로 mutable 상태를 함께 받지 않는다.
   - **zone 변경**: 새 zone spot에 **join**한다(`EnterZoneMsg`가 그 admission payload다). join이
     zone 이동이고, 노드가 바뀌면 그 join이 곧 Actor relocation이다(§2.6). 이전 spot의 퇴장은 framework의
     `OnLeaveActor`가 알려 주므로 앱이 따로 보낼 메시지는 없다(§7.3).
4. zone spot은 받은 값으로 map을 갱신한다. `ZoneStateNotify`는 tick에서 이 map으로 만든다.

> **사본은 한 턴 늦다.** zone spot이 보관하는 좌표는 **사본**이므로(§2.1) actor의 상태 변화보다
> 늦게 반영된다. 특히 relocation 직후에는 출발 zone에 해당 플레이어의 사본이 잠시 남을 수 있다.
> 이 창은 정상이며, 정본은 어떤 tick의 목록이 원자적이라고 약속하지 않는다.

### 2.2 이동 검증 순서

여러 조건을 동시에 위반할 수 있으므로 **순서를 고정**한다. 먼저 걸린 사유 하나만 반환한다.

1. `OutOfRange` — 목표 좌표가 `0..99` 밖
2. `TooFar` — 각 축 이동 거리가 5 초과
3. `DiagonalCrossing` — **X 경계와 Y 경계를 한 번에 통과**한다(예: `(49,49) → (50,50)`).
   비인접 zone으로 직행하게 되므로 거부한다. 인접 zone으로만 이동할 수 있다.
4. `ZoneMaintenance` — target zone의 현재 owner가 join admission을 거부한다(§2.3)

거부되면 좌표를 바꾸지 않고 `MoveRejectedNotify`로 현재 좌표를 반환한다.

### 2.3 점검 모드가 이동에 적용되는 범위

점검 모드는 Ops가 desired state를 저장하고 `NodeMaintenanceChangedEvent`를 fanout할 때
적용한다. Application은 zone과 node의 대응 관계나 transport NodeRid를 저장하지 않는다.
Target zone의 owner가 점검 중이면 그 Spot의 join admission이 이동을 거부한다.

| 이동 | 점검 모드인 노드로 | 판정 |
|---|---|---|
| 같은 zone 안 이동 | (같은 노드) | **허용** |
| 다른 zone으로 join | target owner가 점검 중 | **거부** (`ZoneMaintenance`) |
| `JoinWorldReq` 신규 입장 | target owner가 점검 중 | **거부** |

Source는 target의 NodeRid를 추론하거나 다른 노드의 점검 상태를 미리 판정하지 않는다.
Location Store가 global `SpotId`의 현재 owner를 찾고 target Spot이 자기 상태로 admission을 판정한다.

| 시점 | 경로 |
|---|---|
| 노드 시작 시 | maintenance store에서 **전 노드의** desired state를 읽어 캐시를 채운다 |
| 상태 변경 시 | `Ops`가 **fanout** Channel `zoneworld.broadcast`로 `NodeMaintenanceChangedEvent`를 발행한다. 전 노드가 구독해 packet name으로 handler를 선택하고 캐시를 갱신한다. |

fanout이 여기서 두 번째로 쓰인다 — `Ops`는 노드 목록을 갖지 않은 채 상태 변경을 전파하고,
노드가 늘어도 발행 코드가 바뀌지 않는다.

> **캐시 유실 시.** fanout은 best-effort이므로 캐시가 최신이 아닐 수 있다. 그 경우 출발
> 노드의 cache가 stale이면 application join proposal이 target에 도달할 수 있다. Target
> `OnActorJoin`은 자신의 현재 점검 상태로 admission을 판정하며, 거부하면 relocation capture와 owner commit을
> 시작하지 않는다. Source는 Actor state와 membership을 유지하고
> `MoveRejectedNotify(ZoneMaintenance)`를 push한다. Cache는 조기 거부 최적화이고 target join admission이
> application 정책의 최종 판정 지점이다.

### 2.4 플레이어 목록 규칙

`ZoneStateNotify.Players`는 자기 zone 플레이어와 경계 밴드를 통해 받은 인접 zone
플레이어를 합친 것이다.

- **중복 제거**: 같은 `PlayerId`가 둘 다 있으면 **자기 zone 값을 사용**한다.
- **정렬**: `PlayerId` **UTF-8 byte 오름차순**(ordinal). 언어 기본 문자열 비교(로캘 의존)를
  쓰지 않는다. `PlayerId`는 `[a-z0-9-]{1,32}` ASCII로 제한한다.
- **인접 zone snapshot 교체**: `ZoneBorderEvent`는 `FromZoneId`별 **최신 snapshot으로 통째
  교체**한다(누적하지 않는다). `Tick`이 보관 중인 값보다 **작거나 같으면 무시**한다.
  플레이어가 없으면 빈 목록을 보내며, 이것도 유효한 snapshot이다(전부 제거).
- **만료**: 어떤 `FromZoneId`의 snapshot을 **3 tick(300ms) 동안 받지 못하면 제거**한다.
  발행 zone의 노드가 종료된 경우에 화면에 남지 않게 한다.
- **동일 `PlayerId` 재입장**: 같은 `PlayerId`로 다시 `JoinWorldReq`를 보내면 **같은
  player actor에 새 session이 bind**되고 이전 session의 binding은 해제된다. 좌표와 zone은
  유지된다.

### 2.5 tick 규칙

zone spot 생성 시 `Tick = 0`이다. timer callback은 다음 순서로 동작한다.

1. `Tick`을 1 증가시킨다(**첫 발행은 `Tick = 1`**).
2. 보관 중인 map과 인접 zone snapshot으로 `ZoneStateNotify`를 만들어 push한다.
3. 인접 zone별 `ZoneBorderEvent`를 publish한다(§4.1의 경계 밴드 조건).
4. 만료된 인접 zone snapshot을 제거한다(§2.4).

### 2.6 zone 이동과 relocation

| 이동 | Actor relocation |
|------|------------------|
| target Spot의 owner가 현재 owner와 같음 | 없음 — 같은 node에서 membership만 바뀐다 |
| target Spot의 owner가 현재 owner와 다름 | 있음 — target owner에서 actor가 materialize된다 |

Application은 어느 경우인지 선택하거나 `NodeRid`로 판정하지 않는다. 두 경우 모두 같은 `JoinSpot(ZoneId)`
호출을 사용하고 client 연결은 유지된다.

Relocation은 같은 player actor를 다른 owner에서 계속 실행하는 과정이다. 따라서 global `ActorId`와
`ObjectGeneration`은 유지하고 owner generation만 증가한다. 이동 중 이전 owner로 도착한 message는
[Message Follow](../../spec/18-object-routing.ko.md#24-이전-owner-route에-도착한-message)가 새 owner로 전달한다.
Application은 이전 owner의 `ActorRef`를 저장하거나 새 owner를 직접 찾지 않는다.

Player Actor factory에는 `PreserveStateWith<PlayerActorRelocationAdapter>()`를 등록한다. Player Actor는
좌표·zone의 권위이므로(§2.1) `RecreateOnRelocation`으로 application state를 생략하면 relocation 뒤 좌표를 유지할 수 없다.
Adapter는 state format을 application이 관리하는 opaque bytes로 반환하며 Framework message나 state contract
ID를 사용하지 않는다.

| 단계 | application이 처리하는 값 |
|---|---|
| `Capture` | `PlayerId`, `X`, `Y`, `ZoneId`, `IsBot`, (봇이면) `DirX`, `DirY`를 byte 배열로 encode한다 |
| `Restore` | Target factory가 만든 Actor에 같은 값을 decode해 적용한다 |

Framework는 target factory와 `Restore`를 owner·membership commit 전에 끝낸다. Target의 application version,
maintenance wave, type·adapter capability와 capacity 적합성은 target reservation 전에 Framework가 판정한다.
Application adapter가 target 점검 상태를 별도 relocation protocol로 해석하지 않는다.

### 2.7 봇 — bound session 없는 actor

월드에는 사람이 조종하지 않는 **봇 8마리**가 상시 이동한다. 봇은 브라우저 client 없이도
월드가 동작하는 것을 보이고, actor relocation을 계속 발생시킨다.

**봇은 사람 플레이어와 같은 `PlayerActor` 타입이다.** 차이는 하나뿐이다 — **bound session이
없다.** 그래서 `MoveRejectedNotify`·`ZoneStateNotify` 같은 client push 대상이 아니다.
[07-actor-spot](../../guide/server/07-actor-spot.ko.md)이 설명하는 "client 없이 존재하는
actor — 서버 로직이 `actorId`로 구동하는 봇/NPC"가 이 모양이다.

| 항목 | 값 |
|------|-----|
| 마리 수 | **8** (zone당 2) |
| 생성 | 각 `ZoneNode`가 시작 시 자기가 호스팅하는 zone의 봇을 만든다 |
| 구동 | zone spot의 **봇 timer(500ms)** 가 자기 zone의 봇 actor에게 `BotTickMsg`를 보낸다 |
| 이동 | 봇 actor가 사람과 **같은 이동 경로**(§2.1·§2.2)를 탄다. 검증·zone 변경·relocation이 모두 동일하다 |
| 걸음 | 봇 tick마다 진행 방향으로 **3칸** |
| 반전 | 이동이 거부되면(`OutOfRange`·`ZoneMaintenance` 등) **방향을 반대로 바꾼다**. 다음 tick에 반대로 진행한다 |
| session | **없다.** 봇에게는 어떤 push도 보내지 않는다 |

**경로는 결정적이다.** 무작위 이동을 쓰면 언어별 결과가 달라져 self-check가 불안정해진다.
초기 좌표와 방향을 고정한다.

| `PlayerId` | 초기 `X` | 초기 `Y` | `DirX` | `DirY` | 초기 zone | 넘는 경계 |
|---|---:|---:|---:|---:|---|---|
| `bot-nw-x` | 10 | 15 | `+1` | 0 | `zone-nw` | X → **노드 간 relocation** |
| `bot-nw-y` | 15 | 10 | 0 | `+1` | `zone-nw` | Y → 노드 내부 |
| `bot-ne-x` | 90 | 15 | `-1` | 0 | `zone-ne` | X → **노드 간 relocation** |
| `bot-ne-y` | 85 | 10 | 0 | `+1` | `zone-ne` | Y → 노드 내부 |
| `bot-sw-x` | 10 | 85 | `+1` | 0 | `zone-sw` | X → **노드 간 relocation** |
| `bot-sw-y` | 15 | 90 | 0 | `-1` | `zone-sw` | Y → 노드 내부 |
| `bot-se-x` | 90 | 85 | `-1` | 0 | `zone-se` | X → **노드 간 relocation** |
| `bot-se-y` | 85 | 90 | 0 | `-1` | `zone-se` | Y → 노드 내부 |

한 축으로만 이동하므로 `DiagonalCrossing`(§2.2)에 걸리지 않는다. X 순찰 봇 4마리가 X 경계를
반복해서 넘으므로 **노드 간 actor relocation이 상시 발생**한다.

마리 수와 경로는 설정 값이다. 데모 밀도를 바꾸려면 이 표만 늘린다.

## 3. 서버 구성

ZoneWorld의 Channel 역할과 물리 연결은 [공통 topology 기준](../README.ko.md#channel-역할과-물리-topology-기준)을
따른다. Gateway·ZoneNode·Ops는 `zoneworld.mesh` 하나를 공유한다.
`zoneworld.broadcast` classic fanout과 두 STREAM listener만 별도 연결이다.

| 서버 | 수 | 책임 |
|------|:--:|------|
| `Gateway` | 1 | 브라우저 STREAM(WS) 종단, 인증, session actor bind, actor relay, client push |
| `ZoneNode` | 2 | **entry spot**, zone spot·player actor의 eligible owner, 경계 동기화, 노드 점검 정책 |
| `Ops` | 1 | 관제 콘솔 STREAM(WS) 종단, runtime event 수집, 공지·점검 fanout 발행 |
| location store | 1 | 공유 dependency(Redis). peer 자동 연결 |
| maintenance store | 1 | 공유 dependency(같은 Redis). 점검 모드 **desired state** 보관(§8.4) |

`ZoneNode`는 같은 실행 파일과 같은 object type 등록으로 2개 실행한다. 두 process가 네 global
`ZoneId`에 `GetOrCreate`를 호출해도 Location Store claim에서 한 owner만 확정된다.

| 인스턴스 | object capability |
|---|---|
| ZoneNode A | 네 zone type과 player actor type |
| ZoneNode B | 네 zone type과 player actor type |

**`NodeId`와 `ZoneId`는 다른 식별자다.** `NodeId`는 관제 화면이 현재 process를 구분하는
application label이고 `ZoneId`는 Location Store 전체에서 유일한 global `SpotId`다. `NodeId`로
zone owner를 계산하지 않는다.

### 3.1 entry spot은 `ZoneNode`가 소유한다

player actor는 `ZoneNode`에 존재한다. actor는 생성된 entry Spot에서 zone Spot으로 join하며,
**join이 곧 zone 이동이고 노드를 넘으면 그것이 relocation이다**(§2.6). 그래서 actor를 만드는 자리는
actor를 유지할 노드와 같아야 한다.

`Gateway`는 entry spot을 두지 않는다. MeshNode에 **참여만** 하면 원격 노드의 actor에 session을
bind하고 relay할 수 있다. Gateway에 entry spot을 두면 actor가 Gateway에서 태어나 첫 zone 진입부터
relocation이 되고, Gateway가 player를 잠시 호스팅하는 노드가 된다 — 어느 쪽도 이 샘플이 보이려는
것이 아니다.

```mermaid
graph TD
    B1["browser: game view"]
    B2["browser: ops view"]
    GW["Gateway<br/>STREAM(WS) · session actor bind · relay"]
    OPS["Ops<br/>STREAM(WS) · runtime event<br/>fanout publisher"]
    subgraph ZN["zone nodes"]
      Z1["ZoneNode A<br/>object server"]
      Z2["ZoneNode B<br/>object server"]
    end
    LS[("location store + maintenance store (Redis)")]

    B1 -->|"STREAM(WS)"| GW
    B2 -->|"STREAM(WS)"| OPS
    GW -->|"actor relay"| Z1
    GW -->|"actor relay"| Z2
    Z1 <-->|"border sync · Logical Multicast"| Z2
    Z1 <-->|"framework relocation when owners differ"| Z2
    OPS -->|"announce · channel fanout"| ZN
    OPS -->|"maintenance change · channel fanout"| ZN
    Z1 -->|"spot event report"| OPS
    Z2 -->|"spot event report"| OPS
    GW -. auto connect .-> LS
    Z1 -. register .-> LS
    Z2 -. register .-> LS
    OPS -. observe peers .-> LS
```

client에는 `Gateway`와 `Ops` 주소만 설정한다. zone 노드 주소는 client에 노출하지 않는다.

### 3.2 ZoneNode routing id 자동 할당

ZoneNode는 `SetRoutingIdPrefix("zn")`만 설정한다. Framework는 process를 시작할 때마다
`zn-<lowercase-canonical-uuid-v4>` 형식의 MeshNode RID를 만든다. ChannelName
membership과 Spot·Actor는 같은 MeshNode에 속하지만 별도 transport RID를 만들지 않는다.

| 항목 | 값 |
|------|----|
| routing id prefix | `zn` |
| 발급 형식 | `zn-<lowercase-canonical-uuid-v4>` |
| MeshNode | `zoneworld.mesh` 하나 |
| Entry Spot ID | `zn-entry-<lowercase-canonical-uuid-v4>` |

한 runtime은 새로 발급한 MeshNode RID 하나를 사용한다. `zoneworld.zones`와
`zoneworld.report`는 descriptor의 membership이며 별도 transport identity를 만들지 않는다.

Gateway도 같은 MeshNode에 고정 RID를 설정하지 않는다. Prefix `gw0`을 사용하므로
process를 시작할 때마다 `gw0-<lowercase-canonical-uuid-v4>` 형식의 새 RID를 받는다.

```csharp
var gatewayMesh = options.AddRouteMesh(ZoneWorldNames.Mesh)
    .SetRoutingIdPrefix("gw0") // transport identity는 runtime마다 새로 발급한다.
    .Listen();

gatewayMesh.Objects().Client(); // global ActorId·SpotId operation을 사용한다.
```

필수 구성은 다음과 같다. 언어별 API 표현은 달라도 같은 prefix와 member 구성을 사용한다.

```csharp
var mesh = options.AddRouteMesh(ZoneWorldNames.Mesh)
    .SetRoutingIdPrefix("zn") // 특정 NodeId나 ZoneId를 prefix와 연결하지 않는다.
    .Listen();

mesh.Objects().Server()
    .AddEntrySpot<ZoneEntrySpot>()
    .AddActorFactory<PlayerActor, PlayerActorFactory>(
        ZoneWorldNames.PlayerActorType,
        factory => factory
            .PreserveStateWith<PlayerActorRelocationAdapter>())
    .AddSpotFactory<ZoneSpot>(
        ZoneWorldNames.ZoneSpotType,
        factory => factory.DisableRelocation());

mesh.Channel(ZoneWorldNames.ZoneChannel)
    .Server(); // zone Logical Multicast의 처리 대상 membership이다.
mesh.Channel(ZoneWorldNames.ReportChannel)
    .Client(); // runtime report를 Ops로 보낸다.
```

ZoneNode는 fixed `SetRoutingId(...)`를 호출하지 않는다. Entry Spot ID도 Framework가
같은 prefix와 별도 UUID로 발급한다.

자동 RID 시나리오는 descriptor heartbeat와 expiry의 framework 기본값을 사용한다.
Crash 뒤 남은 descriptor가 expiry될 때까지 새 process의 별도 RID와 함께 보일 수 있으므로
관측 검증은 location reconcile 시간까지 포함한다.

#### `NodeId`, global ZoneId와 자동 transport identity의 관계

`NodeId`와 transport identity는 같은 값이 아니다. `NodeId`는 관제 화면이 process를 구분하는
application label이다. Zone owner는 global `ZoneId`와 Location Store authority가 정한다.

`NodeId`와 transport RID를 읽는 코드는 Admin/Ops 관측과 lifecycle 검증에만
둔다. 게임의 입장·이동·공지·Actor·Spot direct call은 이 값을 입력으로 받지 않는다.

따라서 RID prefix를 특정 `ZoneId`의 owner로 해석하지 않는다. 특정 `NodeId`가
특정 zone을 받도록 예약하는 affinity도 사용하지 않는다.

`ReportNodeStatusMsg`는 `NodeRid`를 application DTO에 싣지 않는다. Ops의 route handler는 framework
context의 `SourceNodeRid`를 현재 `NodeId` report와 연결해 topology 관측 결과를 정리한다.
이 값은 application routing, object owner 추론이나 다음 process의 identity로 사용하지 않는다.

#### 배포와 교체

운영 환경에서는 두 ZoneNode가 같은 실행 이미지와 같은 RID prefix를 사용한다. `NodeId`와 담당
zone은 application topology 설정으로 유지한다. bind endpoint는 서로 다른 pod나 머신에서 같은 값을
사용할 수 있지만, 한 머신에서 여러 process를 실행하는 sample runner는 port 충돌을 피하기 위해
서로 다른 endpoint를 계속 생성한다. runner의 endpoint 차이는 RID를 수동으로 지정하는 배포 정책이
아니다.

한 ZoneNode가 종료된 뒤 시작한 replacement는 새 RID를 발급받고 새 descriptor를
게시한다. 정상 종료면 기존 descriptor를 제거하고, crash면 기존 descriptor가 expiry될
때까지 새 descriptor와 함께 보일 수 있다. Peer는 RID와 lifecycle generation으로 둘을
구분하고 Location Store의 descriptor 변경을 따라 새 endpoint에 연결한다.

`ZW-D2`의 subscriber-only `zone-node-3`은 RouteMesh를 등록하지 않는다. 이 process는
zone과 MeshNode를 호스팅하지 않으며 classic fanout의 동적 참여만 검증한다.

## 4. 사용하는 ZLink 요소

| 역할 | 사용하는 요소 |
|------|---------------|
| `location store` | 공유 저장소 기반 peer discovery, 자동 연결 |
| `Gateway` | stream node(WS), MeshNode membership, 원격 actor에 session bind, actor owner route와 bound session push |
| `ZoneNode` | MeshNode(Entry Spot + zone Spot + player actor + 운영 ChannelName), Logical Multicast, actor cross-node relocation, fanout subscriber, local Spot runtime event |
| `Ops` | stream node(WS), fanout publisher, report ChannelName handler, runtime event(location·socket) |
| client | 브라우저 stream connector(WS) |

> ZoneWorld는 location store descriptor로 같은 MeshName의 peer를 자동 연결한다. manual peer와 자동
> discovery를 같은 MeshNode에서 함께 구성하지 않는다. runner는 role별 MeshNode endpoint 하나만
> 준비하고 Spot·ChannelName·Logical Multicast용 endpoint를 추가하지 않는다.

이름을 다음으로 고정한다.

| 이름 | Framework 요소 | 연결 |
|------|----------------|------|
| `zoneworld.mesh` | RouteMesh | ZoneNode의 RID, ChannelName membership, zone Spot과 player actor를 소유한다. |
| `zoneworld.zones` | ChannelName | zone Logical Multicast의 target membership |
| `zoneworld.broadcast` | **fanout channel** | `Ops`(publisher) → 전 `ZoneNode`(subscriber) |
| `zoneworld.report` | ChannelName | `ZoneNode` → ready `Ops` member. local Spot 이벤트 보고 |
| `WorldAnnounceEvent` | fanout packet (`zoneworld.broadcast`) | 공지 발행 |
| `NodeMaintenanceChangedEvent` | fanout packet (`zoneworld.broadcast`) | 점검 상태 전파(§2.3) |
| `zone.border.<from>.<to>` | Logical Multicast topic (`zoneworld.zones`) | zone spot이 **인접 zone별로 따로** publish |

### 4.1 경계 동기화 topic

**보내는 zone과 받는 zone을 topic 이름에 모두 넣는다.** 하나의 topic을 여러 인접 zone이
구독하면 그 경계와 무관한 플레이어까지 전달되기 때문이다.

| topic | 발행 zone | 구독 zone | 담는 플레이어 |
|---|---|---|---|
| `zone.border.zone-nw.zone-ne` | `zone-nw` | `zone-ne` | `X`가 `40..49` |
| `zone.border.zone-nw.zone-sw` | `zone-nw` | `zone-sw` | `Y`가 `40..49` |
| `zone.border.zone-ne.zone-nw` | `zone-ne` | `zone-nw` | `X`가 `50..59` |
| `zone.border.zone-ne.zone-se` | `zone-ne` | `zone-se` | `Y`가 `40..49` |
| `zone.border.zone-sw.zone-nw` | `zone-sw` | `zone-nw` | `Y`가 `50..59` |
| `zone.border.zone-sw.zone-se` | `zone-sw` | `zone-se` | `X`가 `40..49` |
| `zone.border.zone-se.zone-ne` | `zone-se` | `zone-ne` | `Y`가 `50..59` |
| `zone.border.zone-se.zone-sw` | `zone-se` | `zone-sw` | `X`가 `50..59` |

대각선 zone은 인접이 아니므로 topic이 없다.

## 5. 책임 분리

| 구분 | 담당 | 책임 |
|------|------|------|
| 연결·세션 | `Gateway` session | WS 종단, 인증, 원격 player actor에 session bind, relay, push 전달 |
| actor 생성 | `ZoneNode` entry spot | player actor를 만들고 zone spot으로 join시킨다(§3.1) |
| 좌표 **권위** | player actor | `X`, `Y`, 현재 `ZoneId`의 소유자. 이동 검증(§2.2)과 zone 변경 판정 |
| 구역 상태 | zone spot | `PlayerId → (X, Y, IsBot)` map **사본** 보관(§2.1), 직렬 처리, tick timer, 경계 동기화 |
| 경계 동기화 | zone spot | 경계 밴드 상태를 인접 zone별 topic으로 publish / 구독 |
| 노드 정책 | `ZoneNode` | 점검 모드 — 그 노드의 **모든 zone**에 적용 |
| 관제 | `Ops` | runtime event 관찰, 공지 발행, 노드 지정 호출, desired state 보관 |

레이어 책임과 의존 방향은 다른 정본 샘플과 같다.

| 레이어 | 내용 | 의존 |
|---|---|---|
| `Domain` | `World`(좌표·zone 판정·경계 밴드), `ZoneState`, `MovePolicy` | ZLink 타입을 참조하지 않는다 |
| `Application` | use case, `NodeMaintenancePolicy` | `Domain` + port interface만 참조 |
| `Infrastructure` | ZLink adapter(spot·actor·session·handler), store repository | `Application` port를 구현 |

## 6. 서버 디렉토리 구조

domain logic과 framework adapter를 분리한다. 언어별 문법과 build system은 달라도 아래
책임 분리를 유지한다. 아래 경로는 언어별 샘플 디렉터리
(`framework/languages/<lang>/samples/ZoneWorld/`, §0.2) 아래를 가리킨다.

```text
Server/Gateway/
  Infrastructure/
    ZLink/
      Sessions/
        PlayerSession          WebSocket endpoint and actor packet relay
        PlayerSessionBinder    Resolve ActorRef and bind session

Server/ZoneNode/
  Domain/
    ZoneWorld/
      World                    Coordinates, zone lookup, adjacency, border band
      ZoneState                Player snapshot and adjacent-zone snapshot
      PlayerPosition
      MovePolicy
  Application/
    Zone/
      MoveUseCase
      ZoneTickUseCase
      BotPatrolPolicy
      NodePlayerCensus
    Node/
      NodeMaintenancePolicy
  Ports/
    MaintenanceStorePort
    OpsReportPort
  Infrastructure/
    ZLink/
      Spots/
        ZoneEntrySpot          Actor creation owner
        Handlers/
          PlayerEnterWorldHandler   EnterWorldReq to zone Spot join
          PlayerJoinWorldHandler    JoinWorldReq to JoinWorldRes
          ZoneSpot
          PlayerMoveHandler         MoveMsg
          PlayerBotTickHandler      BotTickMsg
          ZoneTickHandler
          BotTickHandler
          ZoneBorderSubscriptionHandler
          DeliverAnnounceHandler
      Actors/
        PlayerActor
        PlayerActorFactory
        PlayerActorRelocationAdapter   Capture and restore coordinates and zone state
        ZoneNodeBootstrap            Restore maintenance and create zones and bots
      Handlers/
        WorldAnnounceSubscriber
        BroadcastProbeSubscriber     Probe for nodes without hosted zones
        NodeMaintenanceChangedSubscriber
      Monitoring/
        LocalSpotEventHandler
        NodeStatusReporter
    Store/
      MaintenanceStoreRepository

Server/Ops/
  Application/
    Ops/
      NodeRegistry
      AnnouncementService
      MaintenanceService
  Ports/
    MaintenanceStorePort
  Infrastructure/
    ZLink/
      Sessions/
        OpsConsoleSession
      Handlers/
        WatchNodesHandler
        AnnounceWorldHandler
        SetMaintenanceHandler
        NodeDiagnosticsHandler
        ReportSpotEventHandler
        ReportNodeStatusHandler
      Monitoring/
        LocationEventHandler
        SocketEventHandler
    Store/
      MaintenanceStoreRepository
```

각 요소의 책임은 다음과 같다.

| 요소 | 책임 |
|---|---|
| `PlayerSession` | WS 종단, 인증, **모든 packet을 actor로 relay**. join도 relay한다 — 첫 relay가 actor의 노드에 push 경로를 알려 주므로, join을 relay하지 않으면 한 번도 움직이지 않은 플레이어는 아무것도 받지 못한다 |
| `PlayerSessionBinder` | global `PlayerId`로 Actor를 `GetOrCreate`하고 같은 operation이 반환한 `ActorRef`에 session을 바로 bind한다 |
| `ZoneEntrySpot` | player actor 생성. 새 actor를 자기 zone spot으로 join시킨다(§3.1) |
| `World` · `MovePolicy` | 좌표계·zone 판정·인접·경계 밴드, 이동 검증(§2.2) |
| `NodeMaintenancePolicy` | 점검 모드 판정(§2.3)과 전 노드 상태 캐시 |
| `ZoneSpot` · `ZoneState` | `PlayerId → (X, Y, IsBot)` **사본** 보관, tick, 경계 동기화. `ActorRef`나 mutable actor instance를 보관하지 않는다(§8.3) |
| `PlayerActor` | 좌표 권위(§2.1), zone 변경·relocation 판정. 봇도 같은 타입이며 bound session만 없다(§2.7) |
| `PlayerActorRelocationAdapter` | 노드 간 relocation에서 좌표·zone·봇 방향을 opaque bytes로 capture·restore한다(§2.6) |
| `BotPatrolPolicy` · `ZoneNodeBootstrap` | 봇 순찰 규칙(§2.7)과 시작 시 생성 |
| `WorldAnnounceSubscriber` | fanout subscriber → **자기 노드의** zone spot으로 send |
| `LocalSpotEventHandler` | local spot runtime event → `Ops` 보고 |
| `MaintenanceStoreRepository` | desired state — `Ops`는 읽고 쓰고, `ZoneNode`는 **읽기만** 한다(쓰는 것은 관제의 권한이다) |
| `NodeRegistry` | runtime event와 노드 보고를 합쳐 노드 상태 집계 |
| `MaintenanceService` | desired state 기록 + 점검 변경 fanout |
| `NodeDiagnosticsService` | `ReportNodeStatusMsg`로 받은 최신 node 상태 조회 |

## 7. Message 계약

공통 message 계약은 언어 중립 schema로 읽는다. 언어별 구현은 record, class, struct처럼
자기 언어에 맞는 표현으로 같은 필드와 의미를 구현한다.

### 7.1 게임 — 브라우저 ⇄ Gateway (STREAM)

| Message | 방향 | 필드 | 의미 |
|---------|------|------|------|
| `JoinWorldReq` | Client -> Gateway stream | `PlayerId` | 월드 입장을 요청한다. |
| `JoinWorldRes` | Gateway stream -> Client | `PlayerId`, `ZoneId`, `X`, `Y` | 입장한 zone과 시작 좌표를 반환한다. 물리 owner는 application 응답에 노출하지 않는다. |
| `MoveMsg` | Client -> Gateway stream -> player actor | `X`, `Y` | 목표 좌표로 이동을 요청한다(응답 없는 one-way send). |
| `ZoneStateNotify` | zone spot -> actor -> bound session -> Client | `ZoneId`, `Tick`, `Players` | tick마다 현재 zone과 경계 밴드의 인접 zone 플레이어를 push한다. `Players`는 `PlayerId`, `X`, `Y`, `ZoneId`, `IsBot`을 가지며 §2.4 규칙으로 병합·정렬한다. 봇도 목록에 포함된다(§2.7). **bound session이 없는 actor(봇)에게는 push하지 않는다.** |
| `ZoneChangedNotify` | player actor -> bound session -> Client | `PlayerId`, `ZoneId` | 논리 zone이 바뀌었음을 push한다. 물리 relocation 여부는 framework 내부 동작이다. |
| `WorldAnnounceNotify` | zone spot -> actor -> bound session -> Client | `AnnouncementId`, `Text` | 관제 공지를 push한다. client는 `AnnouncementId`로 중복을 제거한다(§8.2). |
| `MoveRejectedNotify` | player actor -> bound session -> Client | `Reason`, `X`, `Y` | 이동 거부와 현재 좌표를 push한다. `Reason`은 `OutOfRange`, `TooFar`, `DiagonalCrossing`, `ZoneMaintenance` 중 하나다(§2.2). |

### 7.2 관제 — 브라우저 ⇄ Ops (STREAM)

| Message | 방향 | 필드 | 의미 |
|---------|------|------|------|
| `WatchNodesReq` | Client -> Ops stream | (없음) | 노드 상태 관찰을 시작한다. |
| `WatchNodesRes` | Ops stream -> Client | `Nodes` | 현재 노드 목록을 반환한다. `Nodes`는 `NodeId`, `Registered`, `Connected`, `Maintenance`, `Zones`, `PlayerCount`를 가진다. |
| `NodeStatusNotify` | Ops stream -> Client | `NodeId`, `Registered`, `Connected`, `Maintenance`, `Zones`, `PlayerCount` | 노드 상태 변화를 push한다(runtime event 기반). |
| `NodeAlertNotify` | Ops stream -> Client | `NodeId`, `Kind`, `Detail`, `OccurredAt` | 노드가 보고한 이상을 push한다. `Kind`는 `TimerHandlerFailed`, `PeersChanged` 중 하나다. |
| `AnnounceWorldReq` | Client -> Ops stream | `Text` | 전 노드 공지를 요청한다. |
| `AnnounceWorldRes` | Ops stream -> Client | `AnnouncementId` | 발행한 공지 id를 반환한다. |
| `SetMaintenanceReq` | Client -> Ops stream | `NodeId`, `Enabled` | 특정 노드의 점검 모드 전환을 요청한다. |
| `SetMaintenanceRes` | Ops stream -> Client | `NodeId`, `Enabled`, `Zones`, `Error` | 전환 결과와 그 노드의 zone 목록을 반환한다. **대상 노드가 미연결이면** `Error=NodeUnavailable`을 반환하고 desired state만 기록한다(노드가 시작할 때 복원된다, §8.4). |
| `NodeDiagnosticsReq` | Client -> Ops stream | `NodeId` | 특정 노드의 진단 정보를 요청한다. |
| `NodeDiagnosticsRes` | Ops stream -> Client | `NodeId`, `Zones`, `PlayerCount`, `Maintenance`, `Error` | 그 노드가 호스팅 중인 zone 목록과 플레이어 수를 반환한다. **대상 노드가 미연결이면** `Error=NodeUnavailable`을 반환한다. |

### 7.3 서버 내부

| Message | 방향 | 필드 | 의미 |
|---------|------|------|------|
| `WorldAnnounceEvent` | `Ops` -> 전 `ZoneNode` (**fanout** `zoneworld.broadcast`) | `AnnouncementId`, `Text` | 노드 목록이나 transport topic 없이 typed event를 전 노드에 발행한다. |
| `NodeMaintenanceChangedEvent` | `Ops` -> 전 `ZoneNode` (**fanout** `zoneworld.broadcast`) | `NodeId`, `Enabled` | 점검 상태 변경을 전 node의 운영 cache에 반영한다. Object owner를 계산하는 입력으로 사용하지 않는다. |
| `DeliverAnnounceMsg` | fanout subscriber -> 현재 node가 호스팅하는 zone Spot | `AnnouncementId`, `Text` | 공지를 받은 node가 local lifecycle에서 확인한 global `ZoneId`들에 direct send한다(§8.2). |
| `BotTickMsg` | zone spot -> 봇 actor (actor send) | (없음) | 봇을 구동한다. 봇 actor가 순찰 규칙(§2.7)으로 다음 좌표를 계산해 이동 경로(§2.1)를 탄다. |
| `EnterWorldReq` | player actor의 Entry Spot handler | `X`, `Y`, `IsBot`, `DirX`, `DirY` | actor가 global `ZoneId`의 Spot에 join해 월드에 들어간다. Gateway는 global `PlayerId`로 `GetOrCreate`하고 framework가 반환한 `ActorRef`를 그대로 session에 bind한다. |
| `EnterWorldRes` | player actor -> caller | `ZoneId`, `X`, `Y`, `Error` | 입장한 논리 zone을 반환한다. 물리 owner는 반환하지 않는다. |
| `ReportSpotEventMsg` | `ZoneNode` -> `Ops` (channel `zoneworld.report`) | `NodeId`, `Kind`, `Detail`, `OccurredAt` | **local** spot runtime event를 보고한다(§8.1). 이벤트 발생 시에만 보낸다. |
| `ReportNodeStatusMsg` | `ZoneNode` -> `Ops` (channel `zoneworld.report`) | `NodeId`, `Zones`, `PlayerCount`, `Maintenance` | 노드 상태를 **1초마다** 보고한다. `Ops`는 이 값으로 `PlayerCount`를 채운다(§8.1). `PlayerCount`는 그 노드의 모든 zone spot이 보관 중인 플레이어 수의 합이다. |
| `ZoneBorderEvent` | zone spot -> 인접 zone spot (**Logical Multicast**, topic `zone.border.<from>.<to>`) | `FromZoneId`, `ToZoneId`, `Tick`, `Players` | 그 경계의 밴드 안 플레이어 목록을 publish한다. 유실을 허용하며 수신측은 §2.4의 교체·만료 규칙을 따른다. |
| `EnterZoneMsg` | player actor -> zone spot (**`JoinSpot` admission payload**) | `PlayerId`, `X`, `Y`, `IsBot`, `InitialEntry` | global `ZoneId`의 Spot에 입장한다. source·target NodeRid와 `ActorRef`는 싣지 않는다. |
| `EnterZoneRes` | zone spot -> player actor | `ZoneId`, `Error` | admission 결과. target owner가 점검 중이면 거부하고 `Error`를 채운다(§2.3). |
| `UpdatePositionMsg` | player actor -> 현재 zone spot (**Spot direct**) | `PlayerId`, `X`, `Y`, `IsBot` | zone이 바뀌지 않은 이동에서 Spot 소유 좌표 사본을 갱신한다. actor와 Spot의 mutable 상태를 같은 handler에 노출하지 않는다. |
| `DeliverZoneStateMsg` | zone spot -> player actor (**Actor direct**) | `ZoneId`, `Tick`, `Players` | actor가 자기 bound session으로 `ZoneStateNotify`를 push하도록 요청한다. |
| `DeliverWorldAnnounceMsg` | zone spot -> player actor (**Actor direct**) | `AnnouncementId`, `Text` | actor가 자기 bound session으로 `WorldAnnounceNotify`를 push하도록 요청한다. |
| `MessageFollowProbeReq` / `MessageFollowProbeRes` | runner -> player actor (**request/reply 검증 전용**) | `ProbeId`, `Payload` | 이전 owner route로 보낸 request가 같은 payload로 한 번 응답하는지 확인한다. 업무 시나리오에서는 사용하지 않는다. |

**lifecycle callback으로 처리하는 동작.** zone 퇴장은 별도 application message를 정의하지 않는다.

| 동작 | 이유 |
|---|---|
| `LeaveZoneMsg` | zone 이동이 `JoinSpot`이므로 이전 spot의 퇴장은 framework의 `OnLeaveActor`가 알려 준다. 앱이 다시 알릴 필요가 없다. |

### 7.4 Actor identity와 session bind

Gateway는 `PlayerId`를 global `ActorId`로 사용해 `GetOrCreate`를 호출한다. Framework가 같은 operation에서
반환한 exact `ActorRef`를 session bind에 바로 사용한다. `ActorRef`나 `NodeRid`를 application DTO로
직렬화하거나 다음 요청을 위해 저장하지 않는다. 이후 Actor·Spot 메시징은 global `ActorId`·`SpotId`로
Location Store의 current owner를 찾는다.

이미 존재하는 User Spot이나 Actor에 보내는 direct call에는 `.InMesh(...)`를 사용하지 않는다.
Global ID가 Location Store 전체에서 유일하므로 Framework가 저장된 Mesh와 current owner를 찾는다.
`.InMesh(...)`는 object가 없어서 최초 배치가 필요한 create 또는 get-or-create intent에만 사용한다.

## 8. 관제

### 8.1 노드 상태 관찰 (runtime event)

`Ops`는 **자기 프로세스에서 관찰 가능한 source만** 직접 구독한다. 원격 노드의 spot
runtime event는 구독할 수 없다(spot event source는 같은 프로세스의 `MeshNode`만 대상으로
한다). 그래서 spot 이벤트는 각 `ZoneNode`가 **local로 받아 명시적으로 보고**한다.

| 관찰 대상 | 방법 | `Ops`가 아는 것 |
|---|---|---|
| 노드 등록·해제 | `Ops`의 **location runtime event** | `Registered` |
| 노드 연결 | `Ops`의 **socket runtime event** | `Connected` |
| zone spot 이상 | `ZoneNode`의 **local spot runtime event** → `ReportSpotEventMsg`(이벤트 시) | `TimerHandlerFailed`, `PeersChanged` |
| 노드 상태·플레이어 수 | `ZoneNode`의 `ReportNodeStatusMsg`(1초 주기) | `Zones`, `PlayerCount`, `Maintenance` |

### 8.2 전 노드 공지 (channel fanout) → 플레이어까지

```mermaid
graph LR
    OPS["Ops"] -->|"(1) channel fanout<br/>WorldAnnounceEvent"| ZN["all ZoneNode<br/>(subscriber)"]
    ZN -->|"(2) send to own zone spots<br/>DeliverAnnounceMsg"| SP["zone spot"]
    SP -->|"(3) actor -> bound session<br/>WorldAnnounceNotify"| C["browser"]
```

- **(1) channel fanout** — **발행 경로는 노드를 하나도 열거하지 않는다.** 노드가 늘어도 발행
  코드는 그대로다. `SendToChannel`이면 `Ops`가 노드 목록을 관리해야 한다.

  > **정확히 말하면.** `Ops`는 status report에서 현재 `NodeId`와 그 node가 보고한 zone 목록을
  > 관측할 수 있지만 이를 owner 배치 설정으로 저장하지 않는다. 공지 발행은 이 관측 결과도
  > 사용하지 않는다. Zone을 호스팅하지 않는 §11.1의 `zone-node-3`도 `Ops` 설정에 등록하지
  > 않은 채 fanout subscriber로 공지를 받는다. `ZW-D2`는 발행자가 target node 목록을
  > 보관하지 않는다는 사실을 검증한다.
- **(2) 자기 노드의 zone spot에만 send (publish 아님)** — spot publish는 mesh 전체가
  대상이므로 모든 노드가 실행하면 각 zone spot이 노드 수만큼 중복 수신한다. Subscriber는
  local Spot lifecycle에서 확인한 global `ZoneId`만 보관하고 그 ID로 direct send한다.
  Framework가 각 Spot의 current owner를 확인하므로 application은 local handle이나 owner RID를
  보관하지 않는다.
- **(3) actor → bound session** — zone spot이 보관 중인 `PlayerId`를 global `ActorId`로 사용해
  send한다. Framework가 각 actor의 current owner를 찾고, actor는 자기 bound session으로 push한다(§8.3).

> **전달 보장 — best-effort다.** 이 경로는 전 구간이 one-way send다. framework는 one-way
> dispatch 실패 시 메시지를 drop할 수 있고, publish 완료가 subscriber 처리 완료를 뜻하지
> 않는다. 그래서 **"정확히 한 번"은 계약하지 않는다.** 대신:
> - **중복은 client가 제거한다.** `AnnouncementId`가 같으면 무시한다.
> - **유실은 허용한다.** 공지는 재전달하지 않는다.
> - actor relocation 중이거나 session이 bind되지 않은 플레이어는 그 공지를 받지 못할 수 있다.
>
> 유실이 치명적인 신호라면 fanout이 아니라 다른 수단(요청/응답, durable store)을 써야 한다.

> **slow joiner.** fanout은 **발행 시점에 구독 중인 노드**에만 전달된다. 공지 후에 시작한
> 노드는 그 공지를 받지 못한다. 공지는 일회성 통지이므로 재전달하지 않는다. **점검 모드는
> 다르다** — 재시작 후에도 유지되어야 하므로 §8.4의 desired state로 복원한다.

### 8.3 zone spot이 actor에게 전달하는 방법

Zone spot은 lifecycle callback에서 확인한 `PlayerId`와 화면에 필요한 좌표 사본만 보관한다.
`ActorRef`, owner `NodeRid`와 mutable Actor instance는 보관하지 않는다. 이런 위치 snapshot은
actor가 relocation된 뒤 이전 owner를 가리킬 수 있기 때문이다.

- `OnActorJoin`은 `EnterZoneMsg`를 검증하고 accept 여부만 결정한다. 승인한 payload는
  `PlayerId`로 찾을 수 있는 pending admission 값으로 보관한다.
- `OnJoinedActor`는 pending 값을 active 좌표 사본으로 옮긴다.
- `OnLeaveActor`는 같은 `PlayerId`의 항목을 제거한다.
- tick과 공지 callback은 `PlayerId`를 global `ActorId`로 사용해
  `DeliverZoneStateMsg` 또는 `DeliverWorldAnnounceMsg`를 보낸다.
- `BotTickMsg`도 global `ActorId`로 보낸다. Framework가 매번 Location Store와 route cache를
  사용해 current owner를 결정한다.

Relocation 중 이전 owner에 도착한 message는 Message Follow가 새 owner로 전달한다. Follow route는
같은 `ActorId`와 `ObjectGeneration`에만 적용된다. Source는 relocation commit에 기록한
target만 사용하며 Follow 중 Location Store에서 다른 owner를 찾지 않는다.

| 보존하거나 제한할 값 | 검증 |
|---|---|
| operation | one-way send와 request/reply 모두 original operation ID, payload와 ObjectGeneration을 유지한다. |
| request terminal | original reply route를 유지한다. Relay가 새 reply route를 만들지 않는다. |
| hop | 최대 8번까지만 relay한다. Loop나 hop 초과는 typed stale-route error다. |
| route queue | route 하나당 최대 1,024 messages, encoded 합계 16 MiB와 negotiated message bound를 지킨다. |
| lifetime | configured Message Follow 기간이 끝나면 이전 route를 사용하지 않는다. 만료·generation mismatch·용량 초과는 typed stale-route error다. |

봇은 bound session이 없으므로 client push 대상이 아니다(§2.7, `ZW-F3`).

Zone spot callback은 직렬 실행되므로 이 map에 lock이 필요 없다. Actor 상태는 Actor turn에서만
바꾸고 Spot 상태는 Spot turn에서만 바꾼다.

### 8.4 특정 노드 점검 모드

```mermaid
sequenceDiagram
    participant C as ops console
    participant O as Ops
    participant S as maintenance store
    participant F as fanout channel
    participant N as zone nodes

    C->>O: SetMaintenanceReq(zone-node-2, true)
    O->>S: write desired state
    O->>F: NodeMaintenanceChangedEvent
    F->>N: publish to every subscriber
    N->>N: apply state by NodeId
    O-->>C: SetMaintenanceRes
```

`Ops`는 application `NodeId`로 desired state를 저장하고 같은 변경을 fanout한다.
모든 ZoneNode가 event를 받지만 자기 `NodeId`에 해당하는 정책만 현재 node 동작에
적용한다. Ops는 transport NodeRid를 application 주소로 사용하지 않는다.

응답은 `NodeRegistry`에 이미 도착한 최신 `ReportNodeStatusMsg`를 사용한다. 대상 node가
등록·연결 상태가 아니면 `NodeUnavailable`을 반환한다. 진단 조회도 같은 최신 report를
읽으며 특정 process에 별도 request를 보내지 않는다.

점검 모드의 의미:

- 그 노드의 **모든 zone**이 신규 입장을 거부한다(`MoveRejectedNotify(ZoneMaintenance)`).
- **이미 그 노드에 있던 플레이어는 유지된다.** 이동·조회가 계속 동작한다.
- 그 노드에서 다른 노드로 나가는 이동은 허용한다.
- 다른 노드는 영향받지 않는다.

**재시작 복원.** `Ops`가 desired state를 **maintenance store(Redis)에 기록**한다.
`ZoneNode`는 시작할 때 자기 `NodeId`의 desired state를 읽어 점검 모드를 복원한다. 노드가
종료된 동안 전환된 상태도 이 경로로 반영된다.

## 9. Client 아키텍처

두 가지를 각각 고정한다. **런타임 패턴**은 단방향 데이터 흐름(§9.1)이고, **폴더 구조**는
Feature-Sliced Design(§9.3)이다. 서버가 헥사고날(§6)인 것과 층위가 다른 결정이며 서로
독립적이다.

### 9.1 런타임 패턴 — 단방향 데이터 흐름 (server-authoritative)

```text
   server push ---> update state ---> render
                         ^               |
                         |               v
                    (no direct write)  input
                         |               |
                         +--- command ---+---> server
```

- **서버가 상태의 유일한 원천이다.** client의 `state`는 서버 push의 투영이다.
- **입력은 `state`를 직접 바꾸지 않는다.** 서버로 명령(`MoveMsg` 등)만 보내고, 서버가
  push한 결과로만 `state`가 바뀐다.
- 이 규칙이 server-authoritative 게임의 전제다. client가 자기 좌표를 임의로 바꾸면
  서버 상태와 어긋난다.

MVU(Model-View-Update)·Flux 계열과 같은 구조이며, 게임 화면과 관제 화면에 같이 적용한다.

### 9.2 기술 스택

| 구분 | 선택 | 이유 |
|---|---|---|
| 언어 | **TypeScript** | connector가 TypeScript다 |
| 빌드 | **Vite** | 설정이 거의 필요 없다 |
| UI·상태 | **Preact + @preact/signals** | 선언형 렌더. signal에 값을 대입하면 화면이 갱신되므로 수동 DOM 갱신 코드가 사라진다 |
| 월드 렌더 | **Canvas 2D** (API 직접) | 격자·플레이어 렌더에는 프레임워크가 코드를 줄여 주지 않는다 |
| 연결 | `@zlink-systems/stream-connector` (브라우저 entrypoint) | §14의 browser 연결 계약 |
| codec | JSON | 샘플 공통 기본값 |
| 테스트 | **Vitest**(domain) + **Playwright**(headless E2E) | `domain`이 브라우저에 의존하지 않으므로 단위 테스트가 가능하다 |

Redux 같은 별도 상태 관리 라이브러리는 두지 않는다. signal이 그 역할을 하며 보일러플레이트가
없다.

### 9.3 client 디렉토리 구조 — Feature-Sliced Design (FSD)

client는 **FSD**로 조직한다. 서버의 헥사고날이 "도메인을 프레임워크에서 분리"하는 규약이라면,
FSD는 "기능 단위로 자르고 계층 간 의존 방향을 고정"하는 프론트엔드 규약이다. 지원 server가
둘이어도 client는 하나이므로, 구조가 흔들리지 않게 규약을 고정한다.

아래 경로는 공통 client 디렉터리(`framework/languages/shared_sample/zoneworld/client/`, §0.2)
아래를 가리킨다.

```text
client/src/
  app/
    game.tsx
    ops.tsx
  pages/
    game/
    ops/
  widgets/
    world-canvas/
    game-hud/
    node-table/
    alert-list/
  features/
    join-world/
    move-player/
    announce-world/
    set-maintenance/
    node-diagnostics/
    watch-nodes/
  entities/
    player/
    zone/
    node/
    announcement/
  shared/
    api/
    config/
    lib/
    ui/
```

| 계층 | 내용 |
|---|---|
| `app` | 진입점(게임·관제), provider, connector 초기화 |
| `pages` | 화면 조립 |
| `widgets` | `world-canvas`(Canvas 2D 월드), `game-hud`(zone·node 표시·공지·연결 상태), `node-table`, `alert-list` |
| `features` | **사용자 상호작용 use case** 하나 = 폴더 하나 |
| `entities` | `player`(내 상태·플레이어 목록), `zone`(zone 판정·경계 밴드), `node`(노드 상태), `announcement`(`AnnouncementId` 중복 제거) |
| `shared` | `api`(ZLink STREAM connector 어댑터), `config`(월드 상수 100×100·band 10·tick 100ms), `lib`, `ui` |

**의존성은 위 계층에서 아래 계층만 참조한다** — `app` → `pages` → `widgets` → `features` →
`entities` → `shared`. 같은 계층의 slice끼리는 직접 import하지 않는다.

`entities/zone`은 서버와 **같은 월드 규칙(§2)** 을 구현한다. 브라우저·WS·ZLink를 참조하지
않으므로 브라우저 없이 단위 테스트할 수 있고, 서버 판정과 client 표시가 어긋나지 않는지
검증한다.

**`features`는 사용자 상호작용 use case에 대응한다** — §7의 메시지 계약과 1:1이 아니다.
서버가 밀어 주는 push 전용 메시지(`ZoneStateNotify`, `ZoneChangedNotify`,
`WorldAnnounceNotify`, `MoveRejectedNotify`, `NodeStatusNotify`, `NodeAlertNotify`)는 feature가
아니라 **해당 `entities`의 model이 적용**한다. 서버 내부 메시지(§7.3)는 client에 나타나지
않는다.

## 10. Client 화면 규격

### 10.0 UI 품질 요구

이 샘플은 **사람이 보고 판단하는 것이 검증 수단**이다(경계 이동, 노드 상태, 점검 격리).
그래서 화면이 읽히지 않으면 샘플의 목적이 성립하지 않는다. 아래를 요구한다.

| 항목 | 요구 |
|------|------|
| **깔끔함** | 화면에 그 순간 판단에 필요한 정보만 둔다. 장식용 요소를 넣지 않는다 |
| **정보 위계** | 가장 중요한 값(현재 zone·node, 노드 이상)이 가장 먼저 눈에 들어와야 한다 |
| **상태를 색으로 구분** | 정상·점검·미연결·경고를 색과 형태로 즉시 구분한다. 색만으로 구분하지 않고 형태·라벨을 함께 쓴다(색각 이상 고려) |
| **변화를 인지 가능하게** | zone 전환·노드 상태 변화·공지 도착은 **짧은 전이(150~250ms)** 로 표시한다. 깜빡임이나 과한 애니메이션은 쓰지 않는다 |
| **일관된 시각 언어** | 게임 화면과 관제 화면이 같은 색·간격·타이포 규칙을 쓴다. 두 화면이 한 제품으로 보여야 한다 |
| **여백과 정렬** | 격자 간격을 일정하게 두고 숫자는 우측 정렬한다. 표는 열 폭을 고정해 값이 바뀌어도 흔들리지 않게 한다 |
| **가독성** | 어두운 배경 + 충분한 명암비(WCAG AA 이상). 좌표·수치는 고정폭 글꼴 |
| **반응성** | tick(100ms) 갱신에도 화면이 끊기지 않아야 한다. Canvas는 `requestAnimationFrame`으로 렌더한다 |

**과하지 않게.** 이 샘플의 주제는 ZLink이지 UI가 아니다. 프레임워크·디자인 시스템을
추가로 들이지 않고, 위 요구를 CSS와 Canvas 렌더링만으로 만족시킨다(§9.2).

아래 §10.1·§10.2의 ASCII 그림은 **정보 배치와 항목**을 정하는 것이고, 실제 화면은 위
품질 요구를 따라 렌더한다.

### 10.1 게임 화면 (`Gateway`에 STREAM 연결)

```text
+----------------------------------------------------+
| ZoneWorld  player-1   zone-nw   zone-node-1  [conn]|
+----------------------------------------------------+
|                        |                           |
|      zone-nw           |        zone-ne            |
|         @ me           |                           |
|            o           |    x  (from adjacent zone)|
|         : band         :                           |
|- - - - - - - - - - - - + - - - - - - - - - - - - - |
|      zone-sw           |        zone-se            |
|                        |                           |
+----------------------------------------------------+
| announce: server maintenance starts in 10 minutes  |
+----------------------------------------------------+
```

범례: `@` = 내 플레이어, `o` = 같은 zone 플레이어, `b` = 봇, `x` = 경계 밴드를 통해 보이는
인접 zone 플레이어, `: band` = 경계 밴드, `[conn]` = 연결 상태.

| 요소 | 요구 | 검증하는 것 |
|------|------|-------------|
| 격자 | 100×100 좌표를 2차원으로 렌더하고 zone 경계선(X=50, Y=50)을 표시한다 | — |
| 경계 밴드 | 경계에서 10 이내 영역을 시각적으로 구분한다 | §7.3 |
| 인접 zone 플레이어 | 같은 zone 플레이어와 **다른 표시**로 구분한다 | 경계 동기화(`ZW-B1`) |
| **봇** | `IsBot=true`인 플레이어를 사람과 **다른 표시**로 구분한다. 8마리가 상시 이동한다 | 봇 actor(`ZW-F1`) |
| 이동 | 방향키로 `MoveMsg`를 보낸다. **좌표를 client가 먼저 바꾸지 않는다** | 단방향 흐름(§9.1) |
| zone 표시 | 현재 `ZoneId`를 항상 표시하고 `ZoneChangedNotify`로 갱신한다 | — |
| 연결 상태 | WebSocket 연결 상태를 표시한다. **zone 이동 중에도 끊기지 않음**을 확인할 수 있어야 한다 | actor relocation(`ZW-B2`) |
| 공지 | `WorldAnnounceNotify`를 표시한다. **같은 `AnnouncementId`는 무시한다** | fanout(`ZW-D1`) |
| 거부 | `MoveRejectedNotify`의 `Reason`을 표시한다 | 점검 모드(`ZW-E1`) |

### 10.2 관제 화면 (`Ops`에 STREAM 연결)

```text
+--------------------------------------------------------------+
| ZoneWorld Ops Console                                        |
+--------------------------------------------------------------+
| node          reg  conn  maint  zones                players |
| node-a         o    o     -     zone-ne                   3   |
|                                 [maint on] [diagnostics]     |
| node-b         o    o     ON    zone-nw,zone-sw,zone-se   1   |
|                                 [maint off] [diagnostics]    |
+--------------------------------------------------------------+
| announce: [__________________________]  [publish to all]     |
+--------------------------------------------------------------+
| alerts                                                       |
| 09:31  node-a       TimerHandlerFailed  zone-ne tick handler |
+--------------------------------------------------------------+
```

| 요소 | 요구 | 검증하는 것 |
|------|------|-------------|
| 노드 목록 | `Registered`·`Connected`·`Maintenance`·`Zones`·`PlayerCount` | runtime event(§8.1) |
| 등록 상태 | `ZoneNode`를 종료하면 `Registered=false`로 표시된다 | location runtime event(`ZW-C2`) |
| 연결 상태 | 연결이 끊기면 `Connected=false`로 표시된다 | socket runtime event(`ZW-C3`) |
| 경고 | `NodeAlertNotify`를 시간순으로 표시한다 | `ReportSpotEventMsg`(`ZW-C4`) |
| 공지 발행 | 텍스트 입력 + 버튼 → `AnnounceWorldReq` | channel fanout(`ZW-D1`) |
| 점검 전환 | **노드별** 버튼 → `SetMaintenanceReq(NodeId)` | desired state + fanout(`ZW-E1`) |
| 진단 | **노드별** 버튼 → `NodeDiagnosticsReq(NodeId)` | 최신 status report(`ZW-E4`) |
| 격리 확인 | 한 노드만 점검 모드로 전환했을 때 다른 노드는 변화가 없어야 한다 | 지정 정확성(`ZW-E1`) |

**관제 화면은 polling하지 않는다.** 노드 상태는 `NodeStatusNotify` push로만 갱신한다.

## 11. Client self-check 기준

| ID | 시나리오 | 성공 기준 |
|----|----------|-----------|
| `ZW-A1` | 입장·이동 | `JoinWorldRes(zone-nw, 25, 25)` 수신 → `MoveMsg` → `ZoneStateNotify`로 좌표 갱신 |
| `ZW-A2` | 이동 검증 순서 | 범위 밖 + 5칸 초과를 동시에 위반 → `Reason=OutOfRange`(§2.2 순서) |
| `ZW-A3` | 같은 zone 플레이어 | 두 client가 같은 zone에 있으면 서로의 `PlayerId`가 `Players`에 있다. 정렬은 `PlayerId` UTF-8 byte 오름차순 |
| `ZW-A4` | **대각선 경계 거부** | `(49,49) → (50,50)` → `Reason=DiagonalCrossing`, 좌표 불변 |
| `ZW-A5` | **같은 zone 좌표 갱신** | zone이 바뀌지 않는 이동 → zone spot의 좌표 사본이 갱신되고 다음 `ZoneStateNotify`에 반영된다 |
| `ZW-B1` | **경계 동기화** | 경계 밴드의 플레이어가 **그 경계를 공유하는 인접 zone**에만 나타난다. **대각선 zone에는 나타나지 않는다** |
| `ZW-B4` | **경계 snapshot 만료** | 인접 zone의 노드를 종료 → 3 tick 뒤 그 zone 플레이어가 `Players`에서 제거된다(§2.4) |
| `ZW-B2` | **서로 다른 owner 사이의 zone 이동** | 운영 probe가 선택한 서로 다른 owner의 인접 zone 경계를 통과 → target `ZoneChangedNotify` + **WebSocket 연결 유지** + 이후 이동 동작. NodeRid는 검증 증거일 뿐 업무 route로 사용하지 않는다 |
| `ZW-B3` | **Y 경계 zone 이동** | Y 경계 통과 → `ZoneChangedNotify(ZoneId=zone-sw)`. 특정 owner NodeRid는 성공 조건이 아니다 |
| `ZW-B5` | **Actor generation 보존** | 운영 probe가 서로 다른 owner의 인접 zone pair를 선택한다. Cross-node relocation 전후 같은 `ActorId`의 `ObjectGeneration`이 유지되고 owner generation만 증가하는지 확인한다. |
| `ZW-B6` | **Message Follow bounded relay** | 선택한 cross-node pair에서 one-way와 request/reply의 payload·operation ID·generation·reply route를 보존하고 hop·message 수·byte·기간 제한과 typed stale-route error를 확인한다. |
| `ZW-C1` | 노드 관찰 | 관제 콘솔이 두 노드를 `Registered=true`, `Connected=true`로 표시. **두 플래그를 모두 확인한다** — 각각 location event와 socket event라는 다른 출처에서 오므로, 하나만 보면 다른 하나의 배선이 동작하지 않아도 통과한다 |
| `ZW-C2` | **노드 종료** | 현재 snapshot에서 `Registered=true`인 node를 선택하고 runner가 그 process를 종료 → 같은 `NodeId`의 `Registered=false` 전이를 확인한다 |
| `ZW-C3` | **연결 단절** | `Ops`↔노드 연결 단절 → `NodeStatusNotify(Connected=false)`(socket event). `ZW-C2`와 같은 이유로 **먼저 `Connected=true`를 확인한 뒤** 전이를 본다 |
| `ZW-C4` | **spot 이벤트 보고** | zone spot tick handler에 예외 주입 → `NodeAlertNotify(TimerHandlerFailed)` |
| `ZW-D1` | **전 노드 공지** | 공지 발행 → **두 노드의 fanout subscriber가 모두 수신**하고, 각 zone spot이 `DeliverAnnounceMsg`를 받는다. client가 받은 `AnnouncementId`에 **중복이 없다**. **`Ops`의 발행 경로가 노드를 열거하지 않음**을 확인한다(§8.2). 전달은 best-effort이므로(§8.2) 개별 플레이어의 수신 누락은 실패로 보지 않는다 |
| `ZW-D2` | **노드 추가 시 공지** | subscriber-only `ZoneNode`를 추가 실행 → `Ops` 코드·설정 변경 없이 fanout subscriber가 공지를 수신한다. 추가 노드의 NodeRid를 발행 경로에 등록하지 않는다 |
| `ZW-E1` | **노드 지정 점검** | Ops가 현재 runtime snapshot에서 선택한 node만 점검 모드로 바뀌고 다른 node 상태는 변하지 않는다 |
| `ZW-E2` | 점검 중 기존 플레이어 | 이미 처리 중인 같은 zone 이동은 계속 수행하고, 점검 node가 owner인 다른 zone의 새 join은 target admission에서 거부한다 |
| `ZW-E3` | 점검 중 이동 | 이동 결과를 미리 정한 zone→node 매핑으로 판정하지 않는다. Target admission 결과와 논리 `ZoneId`만 검증한다 |
| `ZW-E6` | 점검 중 신규 입장 | 점검 모드인 노드의 zone으로 `JoinWorldReq` → 거부된다(§2.3) |
| `ZW-F1` | **봇 존재** | client 접속 직후 `Players`에 `IsBot=true`인 봇이 있고 좌표가 tick마다 변한다. **월드 전체의 봇 8마리는 서버 로그로 확인한다** — client는 자기 zone과 인접 zone 밴드만 보므로 8마리를 한 번에 볼 수 없다(§2.7, §4.1) |
| `ZW-F2` | **봇 zone 이동** | **client를 하나도 연결하지 않은 상태**에서 X 순찰 봇이 X 경계를 넘어 global target Spot에 join한다. owner가 다르면 framework relocation이 동작한다 |
| `ZW-F3` | **봇에 push하지 않음** | 봇에게 `ZoneStateNotify`·`MoveRejectedNotify`를 보내지 않는다(session 미bind actor 대상 push 시도가 없다). **부재이므로 서버 로그로 판정한다** — client는 다른 actor에게 push가 가지 않았음을 관측할 수 없다 |
| `ZW-F4` | **봇 방향 반전** | 점검 모드인 노드로 향하던 봇의 이동이 거부되면 다음 이동부터 반대 방향을 사용한다(§2.7) |
| `ZW-E4` | **노드 진단** | runtime에서 관측한 node를 선택해 `NodeDiagnosticsReq` → 그 시점에 local인 `Zones`와 `PlayerCount` 반환 |
| `ZW-E5` | **재시작 복원** | 현재 snapshot에서 선택한 node를 점검 모드로 전환한 뒤 같은 deployment label의 process를 재시작 → maintenance store에서 복원(§8.4) |

### 11.1 세 번째 노드 (`ZW-D2` 전용)

`ZW-D2`는 "발행자가 노드 목록을 갖지 않는다"를 검증한다. 임시로 추가하는 노드다.

| 항목 | 값 |
|---|---|
| `NodeId` | `zone-node-3` |
| 담당 zone | 없음(zone spot을 만들지 않는다) |
| 역할 | `zoneworld.broadcast` **subscriber만** 등록한다 |
| 검증 | `Ops`의 코드·설정을 바꾸지 않고 **이 노드의 subscriber handler가 `WorldAnnounceEvent`를 수신**한다 |

플레이어를 호스팅하지 않으므로 성공 기준은 **subscriber handler의 수신 로그**다. `Ops`가
노드 목록을 관리하지 않으므로 노드를 추가해도 발행 코드가 바뀌지 않는다는 것이 확인된다.

### 11.2 시나리오 client가 갖춰야 할 연산

**§11.3의 흐름은 아래 연산들로만 쓴다.** 이 어휘가 언어마다 같으면 흐름도 같아진다 — 각 언어는
자기 stream connector 위에 이 얇은 층을 만들고, 시나리오는 connector API가 아니라 이 어휘로 읽힌다.
언어별 `GameClient`와 `OpsClient`는 다음 연산을 같은 의미로 제공한다.

**게임 client** (`Gateway`에 STREAM 연결)

| 연산 | 하는 일 |
|---|---|
| `Connect(gateway, playerId)` | STREAM 연결을 연다. `playerId`는 **매 시나리오 고유**로 만든다(재입장 충돌 방지, §2.4) |
| `JoinWorld() → JoinWorldRes` | `JoinWorldReq`를 request로 보내고 응답을 받는다. 시작 좌표를 client의 현재 위치로 기억한다 |
| `Move(x, y)` | `MoveMsg`를 **one-way send**한다(응답 없음) |
| `WalkTo(x, y) → ZoneStateNotify` | 목표까지 **한 번에 한 합법 걸음씩**(축당 최대 5, §2.2) 이동한다. 각 걸음마다 `Move` 후 그 좌표가 반영된 `ZoneStateNotify`를 기다린 뒤 다음 걸음을 뗀다 |
| `WaitFor<T>(predicate, timeout) → T` | 들어오는 stream 메시지 중 타입 `T`이면서 `predicate`를 만족하는 첫 메시지를 기다린다 |
| `WaitForPosition(x, y)` | `Players`에서 자기 좌표가 `(x,y)`인 `ZoneStateNotify`를 기다린다 |
| `NextTick()` | 자기가 `Players`에 있는 다음 `ZoneStateNotify`를 기다린다 |
| `Collect<T>(window) → [T]` | `window` 동안 도착한 타입 `T` 메시지를 **전부** 모은다. 하나도 없어도 유효한 관측이다(best-effort, §8.2) |
| `Me(state)` | `ZoneStateNotify.Players`에서 자기 `PlayerView`를 꺼낸다 |

**관제 client** (`Ops`에 STREAM 연결)

| 연산 | 하는 일 |
|---|---|
| `Connect(ops)` | STREAM 연결을 연다 |
| `WatchNodes() → WatchNodesRes` | `WatchNodesReq`를 보내 현재 노드 목록을 받는다 |
| `Announce(text) → AnnounceWorldRes` | `AnnounceWorldReq`를 보내고 발행 id를 받는다 |
| `SetMaintenance(nodeId, enabled) → SetMaintenanceRes` | 점검 모드를 바꾼다. 대상 노드가 명령을 적용한 뒤 같은 상태가 `NodeStatusNotify`로 관측될 때까지 기다린다. 대상 노드가 미연결이면 desired state만 기록하고 즉시 반환한다. |
| `ResetMaintenance()` | 두 노드를 모두 점검 해제하고 확인한다. 시나리오가 토폴로지를 공유하므로, 점검을 읽는 시나리오는 **아는 상태에서 시작**해야 한다 |
| `Diagnose(nodeId) → NodeDiagnosticsRes` | `NodeDiagnosticsReq`를 보낸다 |
| `WaitFor<T>(predicate) → T` | 게임 client와 같되 **더 긴 timeout**을 쓴다 — 노드 상태 전이는 러너가 노드를 없애야 일어난다 |

**반드시 그대로 옮길 것.**

- **`WalkTo`는 대각선을 피한다.** 한 걸음이 X·Y 경계를 동시에 넘게 되면(§2.2 `DiagonalCrossing`)
  한 축을 먼저 정리하고 다음 축을 건드린다. 지도를 한 메시지로 건너뛰지 않는다.
- **`SetMaintenance`/`ResetMaintenance`는 고정 시간만큼 기다리지 않는다.** 대상 노드가 연결되어
  있으면 `NodeStatusNotify`에서 요청한 상태를 관측한 뒤 다음 단계로 진행한다. 다른 노드의 캐시가
  아직 이전 값을 가지고 있어도 목표 노드가 자기 상태를 권위로 다시 판정하므로 결과는 달라지지 않는다(§2.3).
- **`playerId`는 시나리오마다 고유.** 재실행이 겹치지 않게 한다.
- **판정 단언**은 세 가지면 충분하다: `True(조건)`, `Equal(기대, 실제)`, `Sequence(기대목록, 실제목록)`
  (순서까지 정확히 일치 — 정본이 "정확한 목록"을 요구하는 곳에 쓴다).

### 11.3 시나리오별 client 흐름

성공 기준은 §11 표가 정본이다. 여기서는 **client가 그 판정에 이르기까지 밟는 단계**를 적는다. 5개
언어가 같은 단계를 밟아야 같은 것을 검증한다. 좌표·zone 이름은 §2, 메시지는 §7을 따른다.

**러너 주도(◆)** 표시가 붙은 것은 client 혼자 끝낼 수 없다 — 러너가 그 사이에 노드를 없애거나
재시작해야 한다(§12). 표시 없는 것은 client가 끝까지 몬다. 서버 로그로만 판정하는 것(`ZW-D2`·`ZW-F2`와
`ZW-D1`·`ZW-F1`·`ZW-F3`의 로그 절반)은 client 흐름이 아니라 러너의 몫이므로 여기 없다(§11·§11.1).

**Track A — 입장과 이동**

- **`ZW-A1`** — `Connect`+`JoinWorld` → 응답이 `(zone-nw, 25, 25)`인지 확인. `Move(28,27)`
  후 `WaitForPosition(28,27)` → 그 `ZoneStateNotify.ZoneId`가 `zone-nw`.
- **`ZW-A2`** — `JoinWorld` 후 `Move(-40, y)`(범위 밖이면서 5칸 초과). `WaitFor<MoveRejectedNotify>` →
  `Reason=OutOfRange`(§2.2 순서), 좌표는 입장 좌표 그대로.
- **`ZW-A3`** — client 둘을 서로 다른 고유 id로 `Connect`+`JoinWorld`(둘 다 `zone-nw`). **양쪽 각각**
  에서 두 id가 모두 있는 `ZoneStateNotify`를 기다리고, 상대가 자기 `Players`에 있는지 확인. 그
  `Players`의 id 목록이 **UTF-8 byte 오름차순으로 정렬**돼 있는지 `Sequence`로 확인(봇 포함 전체).
- **`ZW-A4`** — `JoinWorld` 후 `WalkTo(49,49)`. `Move(50,50)`(두 경계 동시 통과) →
  `WaitFor<MoveRejectedNotify>` → `Reason=DiagonalCrossing`, 좌표는 `(49,49)` 불변.
- **`ZW-A5`** — `JoinWorld` 후 `Move(x+4, y)`(같은 zone). `WaitForPosition` → `Me(state)`의 좌표가
  갱신됐고 `ZoneId`는 여전히 `zone-nw`(zone spot의 사본이 actor 좌표를 따라옴).

**Track B — 경계와 relocation**

`ZW-B5`와 `ZW-B6`를 시작하기 전에 runner의 운영 probe가 네 zone의 current owner를
Location Store에서 읽는다. 변을 공유하면서 owner가 서로 다른 zone pair를 하나 선택하고,
전용 probe Actor를 source zone에 만든다. 이 관측값은 scenario 준비와 검증에만 사용하며
application request의 NodeRid나 placement 입력으로 전달하지 않는다.

서로 다른 owner의 인접 pair가 없으면 cross-node relocation을 검증한 것으로 통과시키지
않는다. Release gate에서는 실패하고, 개발용 탐색 실행에서는 `inconclusive`로 명시해
same-node 이동 결과와 구분한다.

- **`ZW-B1`** — client 셋(west·east·diagonal)을 `JoinWorld`. `east.WalkTo(55,25)`(zone-ne로),
  `diagonal.WalkTo(55,55)`(zone-se로), `west.WalkTo(45,45)`(zone-nw 밴드). **east 관점**에서
  `zone-ne` 상태에 west가 보이는지 기다리고, 그 `PlayerView`의 `ZoneId`가 `zone-nw`(자기 zone으로
  표기)이며 `X>=40`인지 확인. **음성 대조**: `diagonal` 관점에서 `zone-se` 상태를 **만료의 2배 tick**
  동안 반복 관측하며 west가 **한 번도** 나타나지 않는지 확인(대각선 zone은 경계를 공유하지 않음).
- **`ZW-B2`** — 운영 probe가 Location Store의 현재 위치를 읽어 서로 다른 owner의 인접 zone
  pair를 선택한다. Player를 source zone으로 이동한 뒤 공통 경계를 넘어 target zone에 도착했는지
  확인한다. 이어서 같은 WebSocket 연결로 한 번 더 이동한다. Probe가 읽은 NodeRid는 source와
  target이 다른지 검증하는 데만 쓰며 Actor·Spot message의 route 입력으로 전달하지 않는다.
- **`ZW-B3`** — `JoinWorld` 후 `WalkTo(25,48)`. `Move(25,52)` → `WaitFor<ZoneChangedNotify>`에서
  `ZoneId=zone-sw`를 확인한다. 특정 node에 남았다는 가정은 하지 않는다.
- **`ZW-B5` ◆** — 선택한 pair의 cross-node 이동 직전과 commit 뒤 운영 probe가 같은
  `PlayerId`의 location record를 읽는다. `ObjectGeneration`은 같고 owner generation만
  증가했는지 확인한다. 이 값은 검증에만 사용하며 game request의 route 입력으로 전달하지 않는다.
- **`ZW-B6` ◆** — relocation commit 직후 runner가 보존한 이전 owner route로 one-way
  `MoveMsg`와 request/reply probe를 보낸다. Relay 뒤에도 operation ID, generation, payload와
  reply route가 같고 target handler의 업무 효과가 한 번인지 확인한다.
  이어서 최대 8 hop, route당 1,024 messages와 16 MiB 경계를 각각 검증한다. Hop·message 수·byte
  제한 초과, Message Follow 기간 만료와 generation mismatch는 typed stale-route error여야 한다.
  Source가 Follow 중 Location Store를 다시 조회하거나 다른 owner로 hidden retry하면 실패다.
- **`ZW-B4` ◆** — client 둘(west·east). `east.WalkTo(52,25)`(zone-ne), `west.WalkTo(45,25)`.
  **east가 `zone-ne` 소속으로** west에 보일 때까지 기다린다(단순히 "보이면"이 아니다 — relocation
  직후 잠깐 출발 zone 사본에 남는 창을 피한다). **[러너가 `zone-node-2`를 없앤다]**. 그 뒤 west의
  `zone-nw` 상태에서 **east가 없고 `zone-ne` 소속 플레이어도 전부 없는** 상태를 최대 60초 기다린다
  → 비정상 종료된 노드의 플레이어가 만료됐다. `zone-sw`(계속 실행 중인 node-1) 밴드 플레이어는
  유지되어야 한다.

**Track C — 노드 관찰**(관제 client)

- **`ZW-C1`** — `Connect`. `NodeStatusNotify(zone-node-1, Registered)`를 기다려 그 `Zones`에
  `zone-nw`가 있는지 확인. `WatchNodes` → 두 노드 모두 `Registered` **그리고** `Connected`인지
  확인(두 플래그는 출처가 다르다, §8.1).
- **`ZW-C4`** — `Connect`. `WatchNodes`를 **부르기 전에** `WaitFor<NodeAlertNotify>(TimerHandlerFailed)`
  대기를 걸어 둔다(경고가 이미 났을 수 있고 `WatchNodesReq` 응답이 그것을 재생한다). `WatchNodes`
  호출 후 그 경고를 받아 `NodeId`가 결함을 주입한 노드(`zone-node-1`)인지 확인.
- **`ZW-C2` ◆** — `Connect`. `WatchNodes`에서 현재 `Registered=true`인 target을 선택한다.
  **[러너가 선택된 process를 종료한다]**. 같은 `NodeId`의 `Registered=false`를 기다린다.
- **`ZW-C3` ◆** — `ZW-C2`와 같되 플래그가 `Connected`다. **먼저 `Connected=true`를 확인**한 뒤
  **[러너가 노드를 없애면]** `Connected=false` 전이를 기다린다.

**Track D — 전 노드 공지**(게임+관제 client)

- **`ZW-D1`** — 게임 client `JoinWorld` + 관제 client `Connect`. `Announce("...")` → 발행 id를
  받는다. 게임 client가 `Collect<WorldAnnounceNotify>(3초)`로 도착분을 모아 **`AnnouncementId`에
  중복이 없는지** `Sequence`로 확인(한 발행 = 한 플레이어에 최대 한 번). subscriber·zone spot 수신은
  러너가 서버 로그로 판정한다(§8.2).

**Track E — 점검과 진단**(관제 client, 필요 시 게임 client)

- **`ZW-E1`** — `ResetMaintenance` 후 관제 화면의 current node 하나를 선택해
  `SetMaintenance(node,true)`를 호출한다. 선택한 node만 상태가 바뀌고 다른 node의 상태는 그대로인지
  확인한다. **finally**에서 같은 observed node를 해제한다.
- **`ZW-E2`** — 점검 중에도 이미 admission된 player의 같은 zone 이동이 계속되는지 확인한다.
  다른 zone join은 target owner의 현재 점검 상태에 따라 판정하며 source가 결과를 미리 계산하지 않는다.
- **`ZW-E3`** — zone 경계 이동은 논리 `ZoneId`와 target admission 결과로 판정한다. 특정 source·target
  node 조합이나 `Relocated` 값을 성공 조건으로 만들지 않는다.
- **`ZW-E4`** — 현재 runtime에서 관측한 node를 `Diagnose`하고 응답의 local `Zones`와
  `PlayerCount>=0`을 확인한다. 미리 정한 zone 목록은 요구하지 않는다.
- **`ZW-E6`** — `ResetMaintenance` 후 `SetMaintenance(west,true)`. 새 게임 client `Connect`+`JoinWorld`
  → `JoinWorldRes.Error == ZoneMaintenance`(입장 자체 거부). **finally**: 해제.
- **`ZW-E5-arm` ◆** — `SetMaintenance(east,true)` → 응답에 `Error` 없음. **[러너가 `zone-node-2`를
  재시작한다]**.
- **`ZW-E5` ◆** — 재시작 후 `Diagnose(east)` → `Maintenance==true`(store에서 복원, §8.4).
  **finally**: 해제.

**Track F — 봇**

- **`ZW-F1`** — `JoinWorld`. `IsBot=true`가 **zone당 봇 수 이상**인 `ZoneStateNotify`를 기다려 봇들을
  기록. 그 뒤 tick을 돌며(`NextTick`) 기록한 봇들이 **전부 한 번씩 움직였는지** 확인(자기 zone 봇의
  이동). 월드 전체 8마리는 러너가 로그로 센다.
- **`ZW-F3`** — `JoinWorld` + 관제 `Connect`. `Announce(...)`(공지 전달 경로를 밟게 한다).
  `Move(-40, y)`→`WaitFor<MoveRejectedNotify>`(거부 push 경로도 밟게 한다). `NextTick`으로 봇이
  월드에 있는지 확인. **부재(봇에 push 없음)는 러너가 서버 로그로** 판정한다.
- **`ZW-F4`** — `ResetMaintenance` 후 `JoinWorld`. `SetMaintenance(east,true)`. **경계 직전의 X 순찰
  봇**(id가 `-x`로 끝나고 `zone-nw`이며 다음 걸음이 경계를 넘는)이 보일 때까지 기다려 그 봇 id와
  현재 X(peak)를 잡는다. 이후 그 봇의 X가 **peak보다 작아질 때까지**(반전) 기다린다. **finally**: 해제.
  — 봇 이름을 미리 박지 않는다: 봇은 zone을 넘나들어 어느 것이 지금 여기 있는지는 실행마다 다르다.

### 11.4 자동 routing id 검증

이 절은 모든 언어의 ZoneWorld self-check가 충족해야 하는 필수 검증이다.

| ID | 검증 | 성공 기준 |
|----|------|-----------|
| `ZW-G1` | MeshNode RID 형식 | 한 ZoneNode의 MeshNode RID가 `zn-<uuid-v4>` 형식이고 ChannelName·Spot·Actor가 별도 transport RID를 만들지 않는다 |
| `ZW-G2` | 시작 순서 독립 | process 시작 순서를 바꿔 RID가 달라져도 global ZoneId routing과 관측한 node의 점검·진단 호출이 올바르게 동작한다 |
| `ZW-G3` | 정상 교체 | 기존 runtime을 정상 종료한 뒤 replacement가 새 RID와 endpoint를 게시하고 peer가 새 descriptor로 수렴한다 |
| `ZW-G4` | crash 뒤 교체 | ZoneNode를 강제 종료한 뒤 replacement가 이전 process와 다른 RID로 Ready가 된다. 이전 descriptor의 expiry 정리는 framework E2E가 검증한다 |
| `ZW-G5` | 고정 RID 설정 제거 | ZoneNode 설정과 topology에 고정 RID가 없고 `SetRoutingId(...)`를 호출하지 않는다 |

`ZW-G2`는 RID가 application `NodeId`가 아님을 검증한다. `ZW-G3`과 `ZW-G4`는 정상 종료와 crash를
각각 검증하므로 하나로 합치지 않는다. Runner는 Ops가 같은 `NodeId` report를 이전 RID와 다른 새
RID에서 받았는지 확인한다.

## 12. Smoke 실행 기준

언어별 runner는 아래 순서를 따른다.

1. 공유 store(Redis)가 준비됐는지 확인한다.
2. `Ops` 서버를 시작한다.
3. `zone-node-1`, `zone-node-2`를 시작한다.
4. `Gateway` 서버를 시작한다.
5. client(Playwright headless)가 게임 시나리오(`ZW-A*`, `ZW-B*`)를 실행한다.
6. 관제 시나리오(`ZW-C*`, `ZW-D*`, `ZW-E*`)를 실행한다.

3단계의 첫 실행 순서를 `zone-node-2`, `zone-node-1`로 바꿔 `ZW-G2`를 검증한다.
정상 교체는 기존 process를 종료한 뒤 replacement를 시작해 `ZW-G3`으로 검증한다.
Crash 경로는 별도 실행에서 `ZW-G4`로 검증한다.

샘플 성공 로그는 아래 의미를 포함해야 한다.

```text
topology=ready
zoneworld-relocation=completed
zoneworld-border-sync=completed
zoneworld-ops-observe=completed
zoneworld-ops-announce=completed
zoneworld-ops-maintenance=completed
zoneworld=completed
```

## 13. 검증 기준

- TypeScript 브라우저 client 하나가 .NET과 Node.js server에 같은 wire 계약으로 연결된다.
- client에는 `Gateway`와 `Ops` 주소만 설정한다. zone 노드 주소가 노출되지 않는다.
- client의 `state`는 서버 push로만 바뀐다. 입력이 `state`를 직접 바꾸지 않는다.
- 게임 화면과 관제 화면이 §10.0의 UI 품질 요구를 만족한다. 두 화면이 같은 시각 언어를
  쓰고, 상태(정상·점검·미연결·경고)를 색과 형태로 즉시 구분할 수 있다.
- 노드 간 zone 이동에서 **client의 WebSocket 연결이 끊기지 않는다.**
- Actor relocation은 `ActorId`와 `ObjectGeneration`을 유지하고 owner generation만 증가시킨다.
- Cross-node 검증은 Location Store에서 관측한 서로 다른 owner의 인접 zone pair를 사용한다.
  Pair가 없으면 release gate를 통과시키지 않는다.
- Message Follow는 one-way와 request/reply의 operation ID, generation, payload와 reply
  route를 보존한다. 최대 8 hop, route당 1,024 messages·16 MiB와 configured 기간을 넘거나
  generation이 다르면 typed stale-route error로 끝난다.
- 노드 내부 zone 이동에서는 actor relocation이 일어나지 않는다.
- 경계 동기화는 인접 zone별 topic으로 publish하며 대각선 zone에는 전달되지 않는다.
- 전 노드 공지는 channel fanout이며 **발행 경로가 노드를 열거하지 않는다**. Zone을 호스팅하지
  않는 추가 subscriber도 `Ops` 설정에 등록하지 않은 채 공지를 받는다(§8.2, `ZW-D2`). 전달은
  **best-effort**이고 중복은 client가
  `AnnouncementId`로 제거한다.
- peer는 location store descriptor를 사용해 자동으로 연결한다(§3).
- 점검 변경은 desired state와 fanout으로 전달하며 해당 `NodeId`를 가진 node의 모든
  zone에 적용된다. 진단은 최신 node status report를 읽는다.
- 점검 모드는 노드 재시작 후에도 maintenance store에서 복원된다.
- 관제 화면의 노드 상태는 runtime event에서 온다(polling 아님). 원격 spot event는
  `ZoneNode`가 명시적으로 보고한다.
- 관측한 target RID를 application service call이나 object 배치 입력으로 사용하지 않는다.
- `PlayerId`·`ZoneId`·`NodeId`는 명시적 domain id이며 routing id hex를 client에 노출하지
  않는다.

§3.2의 자동 routing ID도 필수 검증 대상이다. ZoneNode마다 prefix와 UUID로 만든
서로 다른 RID를 사용하고, 시작 순서·정상 교체·crash 뒤 교체를 §11.4대로 통과해야 한다.
고정 `NodeRid`는 같은 동작으로 간주하지 않는다.

## 14. TypeScript browser connector 의존

이 샘플의 client는 browser-only TypeScript connector package root를 사용한다. 외부 STREAM
endpoint는 `ws://` 또는 `wss://`여야 하며 HTTP 호출은 runner의 same-origin reverse proxy를
통과한다.

Inbound handler가 시작한 관련 outbound는 `flowFrom(message)`로 표시한다. 표시하지 않은 UI나
timer callback은 새 application flow를 시작하므로 동시에 실행되어도 inbound flow가 이어지지
않는다. Runner는 실제 Chromium에서 WS/WSS, request/reply, push, reconnect와 종료를 검증한다.

```ts
const ws = new WebSocket(endpoint);
ws.binaryType = 'arraybuffer';
ws.send(bytes);            // 프레이밍을 브라우저가 수행한다
ws.onmessage = e => ...;   // 디프레이밍을 브라우저가 수행한다
```

브라우저가 WebSocket handshake와 frame codec을 제공하므로 application code가 이를 다시 구현하지
않는다.

### 14.1 transport 책임 분리

| 계층 | 책임 | 브라우저에서 |
|---|---|---|
| `stream-wire`(ZLink 프레이밍·헤더 codec) | `Uint8Array` 기반 wire 처리 | browser와 공통으로 사용 |
| Runtime(dispatcher, pending request, observer) | transport와 독립적인 메시지 실행 | browser와 공통으로 사용 |
| Transport | 플랫폼 `WebSocket`을 `ZlinkStreamConnection`으로 연결 | browser 구현이 담당 |

Transport 경계는 다음 세 규칙을 따른다.

1. Browser package는 `node:net`·`node:tls`와 Node 전용 flow context에 의존하지 않는다.
2. Native `WebSocket`은 `ZlinkStreamConnection` 구현으로 연결한다.
3. 별도 browser subpath를 만들지 않고 ESM browser runtime을 package root에서 제공한다.

공개 계약은
[TypeScript Stream Connector](../../spec/stream-connector/languages/typescript/03-stream-connector.ko.md)가
소유한다. ZoneWorld runner는 실제 Chromium에서 request/reply, push, reconnect와 명시적 flow 전달을
검증한다.

## 15. 기능 범위

| 기능 | ZoneWorld에서의 사용 |
|---|---|
| channel fanout (`AddFanoutChannel`) | 전 노드 공지와 점검 상태 전파 |
| runtime monitoring event | 관제 노드 상태 |
| actor cross-node relocation | 사람과 봇의 노드 간 경계 이동 |
| bound session 없는 actor | Spot timer로 이동하는 봇 8마리(§2.7) |
| Logical Multicast | 인접 zone별 경계 동기화 |
| 자동 routing ID | Prefix와 UUID로 발급한 MeshNode RID의 교체 수렴(§3.2·§11.4) |
| 브라우저 client | 게임과 관제 UI(§14) |

RouteMesh는 ZoneWorld의 ChannelName, Spot·Actor와 Logical Multicast를 함께 운반한다.
공지와 점검 변경은 별도 classic fanout channel을 사용한다.

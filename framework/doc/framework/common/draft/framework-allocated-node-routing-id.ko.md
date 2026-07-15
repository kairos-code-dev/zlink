# Framework 자동 node routing id 할당 계약 초안

> **구현 전 설계 초안이며 현재 공개 계약이 아니다.** 이 문서는 location store를 사용하는
> 동일 구성의 서버들이 node routing id를 중복 없이 자동 할당받는 목표 계약을
> 정의한다. 공개 API를 추가하거나 구현하기 전에
> [공개 계약 관리](../../spec/00-public-contract-governance.ko.md)의 절차에 따라 공통 spec,
> 언어별 interface와 구현 차이를 먼저 확정해야 한다.

## 1. 문제와 목표

channel과 `SpotNode`의 routing id는 다른 노드가 해당 node를 지목할 때 사용하는 논리 주소다.
현재 application은 `SetRoutingId(...)`로 값을 직접 지정할 수 있다. zone server를
`zone1`부터 `zone100`까지 운영하면 이 방식은 서버마다 다른 설정을 요구한다. 물리
머신이나 process를 교체할 때도 교체 대상의 값을 다시 지정해야 하므로, 동일한 server
image와 동일한 설정으로 수평 배포하기 어렵다.

이 초안의 목표는 application이 다음 규칙만 선언하게 하는 것이다.

```text
channel name: zone
slot count: 100
allocation: lowest available slot
```

framework는 location store에서 사용 가능한 가장 작은 번호를 원자적으로 확보하고,
`zone1`부터 `zone100` 사이의 routing id를 해당 channel 또는 `SpotNode`에 적용한다. 모든
zone server는 같은 설정을 사용한다.

자동 할당한 routing id는 **single-active logical socket/node identity**다. routed channel에서는
전송 목적지와 peer를 구분하는 권한으로 사용하고, fanout에서는 packet tracking, logging과
debugging에서 runtime을 식별하는 단서로 사용한다. 유효한 lease를 가진 runtime 하나만 해당
routing id를 사용한다.

framework는 domain 상태의 단일 writer, exactly-once 처리나 in-flight 요청의 중복 방지까지
보장하지 않는다. timeout과 재전송을 허용하는 업무는 별도의 idempotency 정책을 가져야 한다.

예를 들어 네 process가 차례로 시작하면 `zone1`, `zone2`, `zone3`, `zone4`를
할당받는다. `zone2`의 lease가 끝난 뒤 새 process가 시작하면 새 process는 `zone5`가
아니라 비어 있는 가장 작은 번호인 `zone2`를 할당받는다.

## 2. 범위

### 2.1 P0 범위

- `SetRoutingId(...)`를 제공하는 client/server, fanout, route mesh와 `SpotNode` builder에서
  같은 자동 routing id 할당 정책을 제공한다.
- location store가 allocation group의 slot, owner lease와 generation을 함께 관리한다.
- 동일한 설정으로 시작한 여러 process에 서로 다른 routing id를 할당한다.
- 정상 종료, crash, store 장애와 동시 시작에서도 같은 slot의 동시 사용을 막는다.
- 할당 결과를 해당 native socket이나 `SpotNode`가 bind되기 전에 적용한다.
- application은 framework가 ready 상태에 도달한 뒤 allocation group의 확정된 slot과 member별
  routing id를 읽을 수 있다. 이 조회로 slot을 선택하거나 lease를 직접 변경할 수는 없다.
- 한 framework runtime이 여러 allocation group의 routing id를 자동 할당받을 수 있다.
- 관련된 여러 builder가 하나의 allocation group을 명시하면 같은 slot 번호를 공유한다.
- 공식 Redis extension과 in-memory contract test 구현을 제공한다.

중복 방지 보장은 이 문서의 semantic floor를 충족하는 location store와 자동 할당에 참여하는
runtime 사이에 적용한다. 수동 고정 RID, store 밖에서 만든 socket이나 보증 수준이 낮은 저장소
구성까지 framework가 교정하지 않는다.

channel 유형마다 자동 routing id의 주된 목적은 다음과 같다.

| 유형 | 자동 routing id의 목적 |
|------|------------------------|
| client/server | server 응답 경로, peer 구분과 요청 추적 |
| route mesh | 목적지 routing, pairwise 연결 판단과 peer 구분 |
| `SpotNode` | node routing, spot location과 owner node 구분 |
| fanout | packet tracking, logging과 debugging을 위한 publisher/subscriber runtime 구분 |

fanout RID는 publish 메시지의 목적지를 선택하는 주소가 아니다. 그러나 같은 RID를 여러 runtime이
동시에 사용하면 trace와 로그만으로 실제 publisher나 subscriber를 구분할 수 없다. 따라서 자동
할당 기능은 fanout에도 읽을 수 있는 RID를 제공하고, 다른 channel 유형과 같은 lease 기반
유일성 규칙을 적용한다.

### 2.2 비목표

- user Spot의 `spot rid`나 actor id를 allocation group에서 할당하지 않는다.
- zone의 application 상태를 저장하거나 복구하지 않는다.
- 할당한 slot만으로 application의 zone·partition·업무 shard 배치를 결정하지 않는다. 업무 배치는
  application topology가 소유하며, 자동 할당은 그 배치가 사용하는 runtime RID를 제공한다.
- 물리 머신, container, pod 이름을 routing id로 사용하도록 강제하지 않는다.
- Kubernetes API나 특정 scheduler에 의존하지 않는다.
- slot 번호를 특정 process에 예약하는 affinity 기능을 제공하지 않는다. 모든 process는
  같은 수준의 worker이며 사용 가능한 slot을 확보한다.
- 특정 slot 예약은 제공하지 않는다. `SetRoutingId(...)`와 자동 할당을 조합해 예약 의미로
  해석하지 않는다.

## 3. 용어

| 용어 | 의미 |
|------|------|
| allocation group | 하나의 slot 번호 범위를 공유하는 routing id 할당 영역 |
| group name | store에서 allocation group을 구분하는 이름. 기본값은 channel 이름 |
| group member | 같은 slot을 공유하는 channel 또는 `SpotNode` builder |
| routing id prefix | 할당 번호 앞에 붙이는 문자열. 기본값은 channel 이름이며 선택적으로 override 가능 |
| slot count | allocation group에서 할당할 수 있는 번호의 수. 번호 범위는 `1..slotCount` |
| slot | allocation group 안의 양의 정수 번호 |
| allocated routing id | `routingIdPrefix + slot`로 만든 `RoutingId` |
| owner id | 한 번 실행된 framework runtime instance를 구분하는 임의 식별자 |
| owner token | `OwnerId + Generation`으로 구성한 fencing token |
| slot lease | owner가 해당 slot을 사용할 수 있는 기한 |

channel 이름은 framework 구성에서 중복되지 않는다는 기존 계약을 사용한다. allocation group을
지정하지 않으면 group name은 channel 이름이므로 builder마다 번호를 독립적으로 할당한다. 관련된
여러 builder에 같은 group name을 명시하면 그 builder들은 runtime마다 slot 하나를 공유한다.

group name은 여러 process가 공유하지만 owner id는 runtime을 시작할 때마다 새로 발급한다. 같은
물리 머신에서 process를 다시 시작해도 이전 owner id를 재사용하지 않는다. routing id prefix를
override해도 group identity는 바뀌지 않는다.

## 4. 설계 비교와 적용 범위

### 4.1 기존 방식 — `SetRoutingId(...)`로 고정 RID 지정

각 server 설정에 `zone1`부터 `zone100`까지 값을 넣고 `SetRoutingId(...)`로 적용한다.
RID가 이미 정해진 topology, 소규모 배포, 외부 시스템이 identity를 소유하는 배포에서는 이
방식을 계속 사용한다. 자동 할당 기능은 `SetRoutingId(...)`를 폐기하거나 대체하지 않는다.

다만 동일한 설정의 server를 수평 배포하는 경우에는 server 수만큼 서로 다른 설정이 필요하고,
교체 과정에서 잘못된 값을 지정하면 중복 routing id가 생길 수 있다. 이 초안은 이러한 배포에서
고정 RID 설정을 자동으로 생성하는 것이 아니라, 별도의 자동 할당 방식을 추가한다.

### 4.2 대안 B — 매번 임의 routing id를 만들고 별도 논리 주소를 둔다

process마다 임의 routing id를 만들고 `zone37` 같은 주소는 별도 resolver가 현재 process
id로 변환하게 할 수 있다. 이 방식은 기존 node routing id와 별개인 주소 계층을 하나 더
만들고, 호출 경로마다 변환 규칙을 알아야 한다. 이미 location store와 routing id가 제공하는
위치 해석을 중복하므로 채택하지 않는다.

### 4.3 대안 C — orchestrator ordinal을 routing id로 사용

Kubernetes StatefulSet처럼 안정적인 ordinal을 제공하는 orchestrator에서는 pod index를
`zone1..N`으로 변환할 수 있다. StatefulSet은 stable identity와 기본 `OrderedReady` 정책을
제공하고, 구성에 따라 parallel pod management와 여러 pod의 동시 update도 지원한다. 따라서
단순히 "항상 직렬이라 느리다"는 이유로 이 대안을 제외하지 않는다.

이 방식은 application의 논리 routing id를 특정 orchestrator의 workload identity에 결합하고,
일반 process·VM이나 ordinal이 없는 동질 replica 배포에 적용할 수 없다. 여러 channel이 같은
번호를 공유하는 allocation group과 store 기반 fencing도 별도로 구현해야 한다. 이 초안은
Kubernetes를 포함한 배포 환경에서 동일하게 동작하는 framework 계약을 목표로 하므로 기본
방식으로 채택하지 않는다. orchestrator ordinal을 명시적 고정 RID로 변환하는 배포는 기존
`SetRoutingId(...)`로 계속 구성할 수 있다.

참고: [Kubernetes StatefulSet](https://kubernetes.io/docs/concepts/workloads/controllers/statefulset/)

### 4.4 추가 방식 — location store가 번호 slot을 lease로 할당

location store가 allocation group별 slot 소유권과 lease를 관리한다. framework는 시작 과정에서
slot을 필요한 group마다 하나씩 확보하고, 결과로 만든 routing id를 해당 channel 또는 `SpotNode`에
적용한다. application에는 개수와 선택적 prefix만 노출하고 동시성, lease, 재할당과 fencing은
framework와 store가 처리한다.

이 자동 할당 방식은 호출자 설정을 줄이면서 기존 location runtime의 owner lease와 generation
규칙을 재사용한다. 새 주소 계층을 만들지 않으며, 할당 이후의 메시징은 기존 `RoutingId`
계약을 그대로 따른다.

application은 builder마다 고정 RID 방식과 자동 할당 방식 중 하나를 선택한다. 값이 정해져 있으면
`SetRoutingId(...)`를 사용하고, 동일한 설정의 runtime들이 빈 번호를 확보해야 하면
`UseAllocatedRoutingId(...)`를 사용한다.

## 5. 사용자 설정 계약

### 5.1 기본 이름

`SetRoutingId(...)`를 제공하는 모든 builder는 숫자 개수만 받는 자동 할당 표면을 함께
제공한다. allocation group을 생략하면 channel 이름으로 구분하므로 각 builder가 독립적으로
번호를 할당받는다. routing id prefix를 생략하면 같은 channel 이름을 prefix로 사용한다.

아래 코드는 비규범 `.NET` 목표 API다. 정확한 이름과 시그니처는 정식 언어별 spec에서
확정한다.

```csharp
options.AddSpotMesh("zone")
    // channel 이름 뒤에 1부터 100까지의 번호를 붙여 자동 할당한다.
    .UseAllocatedRoutingId(slotCount: 100)
    .EnableRouter("tcp://0.0.0.0:9001")
    .AddSpotFactory<RoomSpot>();
```

할당 결과는 다음 범위에 속한다.

```text
zone1
...
zone100
```

framework는 prefix와 번호 사이에 구분자를 자동으로 넣지 않는다.

### 5.2 routing id prefix override

channel 이름과 다른 이름이나 구분자를 routing id에 사용하려면 `routingIdPrefix`를 지정한다.
구분자가 필요하면 prefix의 마지막 문자로 직접 포함한다.

```csharp
options.AddSpotMesh("zone")
    // "zone-" 뒤에 번호를 붙여 zone-1부터 zone-100까지 만든다.
    .UseAllocatedRoutingId(slotCount: 100, routingIdPrefix: "zone-")
    .EnableRouter("tcp://0.0.0.0:9001")
    .AddSpotFactory<RoomSpot>();
```

이 설정은 `zone-1`부터 `zone-100`까지 사용한다. `routingIdPrefix: "zone/"`를 지정하면
`zone/1`부터 `zone/100`까지 사용한다. 어느 경우에도 기본 allocation group 이름은 실제
channel 이름인 `zone`으로 유지된다.

같은 allocation group을 사용하는 모든 process는 같은 member 구성과 각 member의 routing id
prefix를 선언해야 한다. member 구성과 prefix는 group metadata에 기록하며, 다른 구성으로 시작한
process는 configuration mismatch로 실패한다.

client/server, fanout, route mesh와 Spot mesh는 모두 같은 메서드 이름과 생성 규칙을 사용한다.
예를 들어 각 builder에서 다음과 같이 설정할 수 있다. 실제 `Add...` 메서드 이름은 언어별
정식 interface에서 현재 builder 표면에 맞춰 확정한다.

```csharp
options.AddClientServer(SampleNames.Gateway)
    // client/server channel의 routing id를 Gateway1부터 자동 할당한다.
    .UseAllocatedRoutingId(slotCount: 100);

options.AddFanout(SampleNames.Events)
    // fanout channel의 routing id를 Events1부터 자동 할당한다.
    .UseAllocatedRoutingId(slotCount: 100);

options.AddRouteMesh(SampleNames.Worker)
    // route mesh channel의 routing id를 Worker1부터 자동 할당한다.
    .UseAllocatedRoutingId(slotCount: 100);
```

### 5.3 여러 builder가 번호를 공유하는 allocation group

서로 관련된 builder가 같은 번호를 사용해야 하면 같은 allocation group을 명시한다. framework는
group마다 slot 하나만 확보하고, 각 member의 channel 이름이나 override한 prefix 뒤에 같은 번호를
붙인다.

```csharp
options.AddSpotMesh("spotnode")
    // zone-server group이 확보한 번호를 spotnode routing id에 사용한다.
    .UseAllocatedRoutingId(slotCount: 100)
    .SetRoutingIdAllocationGroup("zone-server")
    .EnableRouter("tcp://0.0.0.0:9001");

options.AddRouteMesh("routermesh")
    // 같은 group을 지정했으므로 Spot mesh와 같은 번호를 사용한다.
    .UseAllocatedRoutingId(slotCount: 100)
    .SetRoutingIdAllocationGroup("zone-server")
    .EnableServer("tcp://0.0.0.0:9002");
```

첫 runtime은 `spotnode1`과 `routermesh1`을 사용하고, 두 번째 runtime은 `spotnode2`와
`routermesh2`를 사용한다. allocation group은 번호만 공유한다. 각 routing id의 prefix와 실제
socket 역할은 member별 설정을 유지한다.

allocation group을 지정하지 않은 builder는 channel 이름을 자기 group name으로 사용한다.
따라서 서로 관련 없는 channel은 명시적인 공유 설정 없이 같은 번호를 보장하지 않는다.
`SetRoutingIdAllocationGroup(...)`은 `UseAllocatedRoutingId(...)`의 선택 설정이며 두 메서드의
호출 순서는 의미에 영향을 주지 않는다. group만 설정하고 자동 할당을 사용하지 않거나,
`SetRoutingId(...)`와 group을 함께 설정하면 configuration error다.

같은 group에 속한 builder는 다음 계약을 함께 따른다.

- 모든 process가 같은 member channel 집합, `slotCount`와 member별 prefix를 선언한다.
- 한 runtime은 group에서 slot 하나만 확보한다.
- 모든 member에 routing id를 적용한 뒤 socket을 bind한다.
- 한 member의 bind가 실패하면 group의 모든 member를 중지하고 slot을 반환한다.
- 정상 종료에서는 group의 모든 member socket을 닫은 뒤 slot을 반환한다.

### 5.4 직접 지정과의 관계

기존 `SetRoutingId(routingId)`는 값이 이미 정해진 배포에 계속 사용한다.
`SetRoutingId(...)`와 `UseAllocatedRoutingId(...)`는 서로 배타적이다. 한 builder에서 둘을
함께 호출하면 호출 순서와 관계없이 configuration error다. 마지막 호출이 앞선 설정을
조용히 덮어쓰지 않는다.

| 설정 | identity 정책 |
|------|---------------|
| 둘 다 없음 | 기존 기본 routing id 정책 |
| `SetRoutingId(...)`만 사용 | 호출자가 지정한 routing id를 그대로 사용 |
| `UseAllocatedRoutingId(...)`만 사용 | location store에서 가장 작은 빈 slot을 할당 |
| 둘 다 사용 | configuration error |

두 설정을 함께 사용했을 때 `SetRoutingId(...)`를 prefix나 선호 slot으로 해석하지 않는다.
지정한 번호가 사용 중이면 다른 번호를 받는 fallback도 제공하지 않는다. 이런 문맥별 의미
변경은 직접 값을 설정하는 기존 메서드의 계약을 흐리고, 사용하지 않는 예약 정책까지 P0
store 계약에 추가하기 때문이다.

같은 allocation group에 참여하는 process들은 고정 RID 방식과 자동 할당 방식 중 하나만 선택해야 한다.
일부 process는 `SetRoutingId(...)`를 사용하고 나머지는 자동 할당을 사용하는 혼합 배포는
P0에서 지원하지 않는다. 자동 할당을 선택한 group의 모든 process는 같은 member 구성과
`slotCount`를 선언한다.

자동 할당을 설정하지 않은 builder의 기존 routing id 동작은 바꾸지 않는다.

### 5.5 입력 검증

- channel 이름 또는 명시적으로 지정한 allocation group 이름은 비어 있거나 공백만으로 구성할
  수 없다.
- `routingIdPrefix`를 지정하면 비어 있거나 공백만으로 구성할 수 없다.
- `slotCount`는 1 이상이어야 한다.
- 생성한 UTF-8 routing id는 core의 `RoutingId` 길이 계약인 1..255 bytes를 만족해야 한다.
- 같은 allocation group을 참조하는 모든 process는 같은 member channel 집합, member별
  `routingIdPrefix`와 `slotCount`를 선언해야 한다.
- location store가 등록되지 않았으면 자동 할당 설정은 startup 전에 configuration error다.
- allocation group만 설정하고 자동 할당을 설정하지 않으면 configuration error다.
- builder에서 routing id를 적용할 socket이나 node 역할이 활성화되지 않으면 자동 할당을
  허용하지 않는다.

### 5.6 lease 시간 설정

자동 할당은 기존 location runtime의 owner lease를 사용하므로 시간 설정도
`AddLocationStore(...)`에 넣지 않고 `ConfigureLocations()`가 반환하는 runtime 공통 옵션에 둔다.
store 설정은 연결 문자열과 key prefix처럼 저장소 구현에 필요한 값을 다루고, lease 갱신 주기와
self-fencing 시점은 framework runtime이 해석한다.

기존 `HeartbeatInterval`과 `OwnerLeaseTtl`을 그대로 사용하고, 자동 할당 RID를 위한
`RoutingIdFencingMargin`과 `OwnerLeaseRenewTimeout`을 같은 옵션에 추가한다. 이 값들은
allocation group별 설정이 아니다. 한 runtime의 모든 allocation group이 owner lease 하나를
공유하므로 group 또는 channel builder에 서로 다른 값을 지정하는 API는 제공하지 않는다.

아래 코드는 `.NET` 설정 예다. 각 호출이 정하는 값은 코드 주석에 표시했다.

```csharp
var locations = options.ConfigureLocations();
locations.HeartbeatInterval = TimeSpan.FromSeconds(10); // owner lease 갱신 주기
locations.OwnerLeaseTtl = TimeSpan.FromSeconds(30); // 갱신하지 못했을 때 lease가 만료되는 시간
locations.RoutingIdFencingMargin = TimeSpan.FromSeconds(5); // 만료 전에 socket을 닫기 위한 여유 시간
locations.OwnerLeaseRenewTimeout = TimeSpan.FromSeconds(3); // renew 한 번의 응답을 기다리는 최대 시간
```

네 값을 지정하지 않으면 §8.4의 기본값을 사용한다. 모두 0보다 커야 하며
`H + R < T - M`을 만족해야 한다. framework가 설정한 margin 안에 readiness 차단과 관련 socket
종료를 완료할 수 없으면 startup configuration error다. 검증은 location store acquire와 socket
bind보다 먼저 수행한다.

`RoutingIdFencingMargin`과 `OwnerLeaseRenewTimeout`은 자동 할당 RID가 하나 이상 설정된 runtime에만
self-fencing 계약으로 적용한다. 자동 할당을 사용하지 않는 application의 기존 location 장애
정책은 바꾸지 않는다. `HeartbeatInterval`과 `OwnerLeaseTtl`의 기존 location runtime 의미도
유지한다.

정식 계약 승격 시 `HeartbeatInterval`과 `OwnerLeaseTtl`의 framework 기본값은 기존 5초와 15초에서
10초와 30초로 변경한다. 두 option의 의미는 그대로지만, 값을 명시하지 않은 기존 application은
heartbeat 빈도와 crash 뒤 owner row 만료 시간이 달라진다. 언어별 spec, implementation gap과
release note에 이 기본값 변경을 기록한다. 기존 시간을 유지해야 하는 application은 두 값을
명시적으로 설정한다.

### 5.7 할당 결과 조회와 entry spot

application은 framework가 `Ready`에 도달한 뒤 allocation group 이름으로 확정된 결과를 기다릴 수
있다. 결과에는 group name, slot과 member channel별 routing id가 포함된다. 이 표면은 로그, 운영
보고와 이미 결정된 application topology에 실제 RID를 연결할 때 사용한다. slot 선택·예약·반환이나
lease 갱신은 노출하지 않는다. 등록되지 않은 group을 조회하면 configuration error이고,
`WaitingForSlot`이면 할당이 확정될 때까지 비동기로 기다린다. runtime이 `Fenced`로 전이하면 대기는
실패하고 host 종료 절차를 따른다.

`SpotNode`가 자동 RID와 entry spot을 함께 사용하면 entry spot routing id는 할당된 `SpotNode` RID를
기본값으로 사용한다. 같은 builder에서 `UseAllocatedRoutingId(...)`와
`SetEntrySpotRoutingId(...)`를 함께 설정하면 configuration error다. 고정 RID를 사용하는 기존
builder의 entry spot 규칙은 바꾸지 않는다.

대표 적용 문서는 [ZoneWorld sample](../sample/zoneworld/README.ko.md)과
[Bingo sample](../sample/bingo/README.ko.md)이다. ZoneWorld는 Spot mesh, spot bridge와 보고 channel이
하나의 번호를 공유하는 예를 제공한다. Bingo는 Play channel과 room Spot mesh가 번호를 공유하고,
Session이 `play1`, `play2` 같은 논리 RID로 현재 slot owner를 선택하는 예를 제공한다. 자동 slot은
transport identity이며 application의 zone이나 room 상태를 저장하지 않는다.

## 6. routing id 생성 규칙

할당 번호는 1부터 시작하며 0을 사용하지 않는다. 정수는 선행 0 없는 ASCII 10진수로
표현한다.

```text
routingIdText = routingIdPrefix + decimal(slot)
```

예:

| routing id prefix | slot | routing id |
|-------------------|-----:|------------|
| `zone` | 1 | `zone1` |
| `zone-` | 37 | `zone-37` |
| `zone/` | 100 | `zone/100` |

store는 routing id 문자열을 다시 파싱해 allocation group이나 slot을 얻지 않는다. slot row에는
group name과 숫자 slot을 typed field로 따로 저장한다. group metadata에는 member channel과
prefix를 별도 field로 기록한다. routing id 문자열 규칙은 언어마다 같은 byte 값을 만들기 위해
고정한다.

## 7. 할당 계약

### 7.1 가장 작은 빈 번호

`AcquireRoutingIdSlot(groupName, members, slotCount, ownerId, leaseTtl)`은
`1..slotCount`를 오름차순으로 판단해 현재 유효한 owner가 없는 첫 slot을 할당한다.
번호를 임의 선택하거나 round-robin으로 선택하지 않는다.

다음 상태에서 `zone1`, `zone3`, `zone4`가 사용 중이고 `zone2`가 비어 있으면 결과는
항상 `zone2`다.

lowest-available은 번호를 조밀하게 유지하는 대신 동시 cold start에서 낮은 slot에 경합이
집중될 수 있다. 매 acquire마다 1번부터 선형 탐색하는 단순 구현은 N개 할당에 최악 O(N²)의
slot 비교를 수행한다. 이 복잡도는 결과 계약이 아니라 구현 선택이며, store는 같은 최소 번호
결과를 유지하는 정렬된 free-slot 구조를 사용할 수 있다. P0의 주 사용 범위인 100개 수준을
넘어 일반화할 때는 구현체별 경합과 탐색 비용을 별도로 검증한다.

### 7.2 원자성

빈 slot 선택, generation 증가, slot owner 기록과 최초 owner lease 기록은 store의 단일
원자 연산이어야 한다. runtime이 목록을 읽고 application process에서 번호를 고른 뒤 별도
write로 claim하는 방식은 허용하지 않는다.

동시에 여러 runtime이 요청해도 한 slot의 `Acquired` 결과는 하나의 owner에게만 반환된다.
나머지 요청은 다음 빈 slot을 받거나 allocation group이 가득 찬 결과를 받는다.

### 7.3 재시도와 멱등성

같은 `ownerId`가 응답 유실 때문에 acquire를 다시 호출하면 이미 확보한 같은 slot과 같은
generation을 반환하고 lease를 갱신한다. 같은 owner가 한 allocation group에서 두 slot을 얻지
않지만, 서로 다른 group에서는 각각 한 slot을 얻을 수 있다.

### 7.4 allocation group 구성 일치

group의 첫 acquire가 정렬된 member channel과 prefix 목록, `slotCount`를 group metadata로
고정한다. 이후 같은 group name에 다른 member 구성, prefix 또는 `slotCount`가 들어오면
`GroupConfigurationMismatch`로 실패한다. 더 큰 값으로 조용히 확장하거나 더 작은 값으로
축소하지 않는다.

group 구성과 개수는 첫 acquire 뒤 변경할 수 없다. 운영 중 변경 API는 제공하지 않으며,
운영자가 store 값을 직접 수정하는 것도 지원 절차가 아니다.

### 7.5 identity 정책 일치

첫 자동 acquire는 group metadata에 `IdentityMode=Allocated`를 함께 고정한다. framework는
startup에서 group member channel의 유효한 fixed peer와 allocated metadata 충돌을 확인하고,
확인된 혼합 구성은 configuration error로 거부한다. allocated group이 확인된 channel에 fixed
peer를 등록하려는 경우에도 같은 오류를 보고한다.

P0의 혼합 검사에는 peer registry와 allocator의 여러 key를 묶는 원자적 transaction을 요구하지
않는다. 따라서 fixed peer 등록과 group 생성이 동시에 경쟁하면 best-effort 감지가 둘을 모두
막는다고 보장하지 않는다. strict single-active 보장은 같은 routing id slot allocation
capability를 사용하는
runtime끼리 적용하며, fixed RID와 자동 할당을 같은 channel에 혼합하는 배포는 지원하지 않는 운영
구성이다. 혼합 배포를 저장소의 교차 key transaction으로 원자 차단하는 기능은 이 계약에
포함하지 않는다.

P0에서는 한 번 `Allocated`로 고정한 group을 fixed 방식으로 되돌리는 관리 표면을 제공하지
않는다. identity 정책을 바꿔야 하면 별도 전환 계약을 먼저 설계한다.

### 7.6 allocation group 소진

모든 slot에 유효한 owner가 있으면 결과는 `GroupExhausted`다. framework는 임의 routing id나
범위 밖의 번호를 만들지 않는다.

기본 runtime 동작은 socket을 bind하지 않은 채 allocation 대기 상태를 유지하고 readiness를
false로 둔다. location store의 watch가 있으면 변경 알림으로 다시 시도하고, 없으면 location
polling 주기에 맞춰 다시 시도한다. host 종료 cancellation은 대기를 끝낸다.

`GroupConfigurationMismatch`, 잘못된 입력과 계약을 지원하지 않는 store는 재시도로 해결할
수 없으므로 startup error로 끝낸다.

## 8. lease, fencing과 중복 방지

### 8.1 소유권 모델

slot row는 최소한 `GroupName`, `Slot`, `OwnerId`, `Generation`, `UpdatedAt`을 가진다. group
metadata는 member별 `ChannelName`과 `RoutingIdPrefix`를 가진다. 생존 여부는 row의 존재가 아니라
같은 물리 저장소의 owner lease로 판단한다.

owner lease는 runtime instance당 하나다. 한 runtime이 여러 group에서 slot을 확보하면 모든 slot
row가 같은 `OwnerId`를 참조한다. group별 소유권은 각 slot row가 나타내며, owner lease를
group이나 builder마다 중복 생성하지 않는다.

generation은 같은 slot이 새 owner에게 다시 할당될 때마다 증가한다. release와 갱신은
`OwnerId + Generation`이 현재 row와 일치할 때만 성공한다. 이전 owner의 늦은 요청은 새
owner의 row를 변경하지 못한다.

### 8.2 정상 종료

정상 종료 순서는 다음과 같다.

1. 신규 배치와 신규 join을 중단하고 기존 작업을 drain한다.
2. 자동 할당한 routing id를 사용하는 모든 socket과 `SpotNode`를 중지한다.
3. owner가 등록한 location row를 제거한다.
4. 현재 owner token으로 확보한 모든 slot을 release한다.
5. owner lease를 제거한다.

socket을 닫기 전에 slot을 먼저 반환하면 다른 runtime이 같은 routing id를 할당받는 동안 이전
runtime이 메시지를 받을 수 있으므로 순서를 바꾸지 않는다.

### 8.3 crash와 재할당

runtime이 release하지 못하고 종료되면 store 기준 lease 만료 뒤 slot을 다시 할당할 수 있다.
새 owner는 같은 slot과 증가한 generation을 받으며, 각 group member의 routing id와 endpoint를
새 peer location row로 게시한다. 다른 runtime은 location reconcile을 통해 같은 논리 routing
id의 새 endpoint로 연결 대상을 바꾼다.

Spot 메시징에서는 자동 할당 값이 owner node rid로 사용되며, 공개 주소 모델은
[SpotHandle 기반 메시징](../../spec/server/24-spot-address-messaging.ko.md)을 따른다. `SpotHandle`은
논리 spot identity를 유지하고 내부 `SpotRef`의 node rid snapshot만 갱신한다. 이 초안은 node rid의
할당 출처를 바꾸지만 `SpotHandle`, `SpotRef`나 spot rid의 의미를 바꾸지 않는다.

### 8.4 store 장애 중 lease 안전성

일반 location 연결은 store 장애 중 마지막 연결 판단을 유지하는 fail-static 정책을 사용한다.
그러나 자동 할당 routing id의 single-active identity는 lease가 만료된 뒤에도 이전 runtime이
계속 해당 RID를 사용하면 보장할 수 없다.

자동 할당 owner는 마지막으로 확인한 lease 만료 시각보다 먼저 해당 routing id의 서비스
제공을 중단해야 한다. store 연결이 복구되지 않아 안전한 갱신을 확인할 수 없으면 framework는
다음 순서로 처리한다.

1. lease 안전 기한 전에 readiness를 false로 바꾸고 신규 작업을 받지 않는다.
2. 안전 기한에 도달하기 전에 자동 할당한 routing id를 사용하는 모든 socket을 닫는다.
3. lease가 만료된 뒤 다른 runtime이 slot을 확보할 수 있게 한다.

store 장애를 이유로 임의의 새 slot을 선택하거나 기존 slot을 lease 없이 계속 사용하지 않는다.
이 규칙이 있어야 network partition에서도 같은 routing id를 가진 두 runtime의 동시 서비스를
막을 수 있다.

owner lease는 runtime당 하나이므로 self-fencing도 runtime 단위로 적용한다. fanout RID가 publish
목적지 주소가 아니더라도 fanout socket만 예외로 유지하지 않는다. fanout-only runtime도 lease
안전성을 확인할 수 없으면 관련 socket을 닫고 self-fence한다. 이는 routing 정합성이 아니라 같은
RID를 가진 publisher/subscriber가 동시에 유지되어 trace와 로그 identity가 모호해지는 것을 막기
위한 의도된 가용성 선택이다.

lease 판정은 [location runtime](../../spec/server/40-location-runtime.ko.md)과 같이 store 시각과
local monotonic 경과 시간을 사용한다. application node의 wall clock으로 만료 여부를 계산하지
않는다.

heartbeat, TTL과 안전 margin은 각각 독립된 option이 아니라 다음 관계를 만족하는 하나의 안전
계약이다.

```text
H = heartbeat interval
T = owner lease TTL
M = fencing safety margin
Q = readiness 차단과 모든 관련 socket 종료에 필요한 최악 시간
R = 허용할 최대 lease renew 지연

fenceDeadline =
    lastConfirmedMonotonic
    + (LeaseExpiresAt - StoreNow)
    - M

M >= Q + local scheduling uncertainty
H + R < T - M
```

framework는 마지막으로 성공한 renew 응답의 `LeaseExpiresAt - StoreNow`를 local monotonic
deadline으로 변환한다. `fenceDeadline` 전에 readiness를 false로 바꾸고, 실제 lease 만료 전에
모든 관련 socket을 닫아야 한다. option 설정이 위 관계를 만족하지 않으면 startup configuration
error다. 일반 location runtime의 `store failure grace`가 더 길더라도 자동 할당 RID에는 이
deadline이 우선한다.

P0 기본값은 `H=10s`, `T=30s`, `M=5s`, `R=3s`다. 기본값은
`H + R = 13s < T - M = 25s`이므로 renew 결과 확인과 fencing 시작 사이에 12초의 여유가 있다.
readiness 차단부터 모든 관련 socket의 강제 종료까지 걸리는 `Q`와 local scheduling uncertainty의
합은 5초 이하여야 한다. 구현체는 이 시간 상한을 넘는 graceful drain을 기다리지 않고 socket을
강제로 닫아 single-active identity의 유일성을 우선한다. 사용자가 값을 override하더라도 위
관계식을 만족해야 한다.

### 8.5 generation data-path fencing을 사용하지 않는 이유

모든 message에 slot generation을 실어 보내고 수신측이 이전 generation을 거부하면 stale
caller와 이전 socket이 겹치는 구간을 더 줄일 수 있다. 그러나 현재 location 계약은 generation을
node 사이에 배포하지 않고 store 내부 owner fencing에만 사용하며, Spot 메시징도 generation을
application 주소에서 숨긴다.

data-path fencing은 wire metadata, 수신측 최신 generation 상태와 stale 응답을 모든 channel에
추가하는 별도 전송 계약이다. 중복 transport routing id가 dispatch 전에 만드는 연결 충돌도
별도로 해결해야 한다. 따라서 이 계약은 generation을 data path에 전달하지 않으며, lease와
시간 기반 self-fencing만 사용한다.

## 9. startup과 rollout

### 9.1 startup 순서

자동 할당은 channel이나 `SpotNode` 생성 이후에 덧붙이는 background 작업이 아니다. routing
id는 native node가 bind되기 전에 고정되어야 하므로 framework startup이 전체 순서를 소유한다.

1. framework registration과 자동 할당 설정을 검증한다.
2. runtime instance용 `ownerId`를 발급한다.
3. 첫 allocation group의 slot과 owner lease를 원자적으로 확보한다.
4. **첫 acquire 응답 직후 owner lease heartbeat와 fencing deadline 감시를 시작한다.**
5. 나머지 group을 결정적인 순서로 순회하며 slot을 확보한다. 같은 owner의 후속 acquire는 기존
   lease를 확인하고 갱신한다.
6. 할당된 각 routing id를 해당 channel, `SpotNode`와 Entry Spot 설정에 적용한다.
7. router·pub/sub socket을 bind한다.
8. peer·entry spot 등 location row를 게시한다.
9. readiness를 true로 바꾸고 메시지 처리를 시작한다.

5단계에서 어느 group이 소진되면 framework는 이미 확보한 다른 group의 slot을 반환한 뒤 socket을
bind하지 않고 전체 allocation을 다시 시도한다. 일부 slot만 보유한 채 대기하지 않는다.
acquire 이후 heartbeat 갱신이나 fencing 안전성을 확인하지 못하면 RID 적용·bind·readiness로
진행하지 않는다. startup이 실패하면 framework는 열린 socket을 정리하고 현재 owner token으로
확보한 모든 slot을 release한 뒤 heartbeat를 중지한다. release 실패 시 lease 만료가 최종 정리를
담당한다.

이 순서는 [SpotNode §2.1](../../spec/server/21-spot-node.ko.md#21-적용-순서)의 bind 전 routing id
적용 규칙을 유지한다.

### 9.2 rollout semantics

`slotCount`가 실행 중인 replica 수와 같으면 surge로 시작한 새 runtime은 기존 owner가 slot을
반환하기 전까지 `WaitingForSlot` 상태다. 새 runtime을 먼저 ready로 만든 뒤 이전 runtime을
종료하는 일반적인 surge 순서는 같은 RID의 single-active identity와 양립할 수 없다.

계획된 slot handoff 순서는 다음과 같다.

1. 이전 runtime이 readiness를 false로 바꾸고 신규 작업을 중단한다.
2. 기존 작업을 drain하고 group의 모든 socket을 닫는다.
3. location row를 제거하고 slot을 release한다.
4. 새 runtime이 같은 slot과 증가한 generation을 acquire한다.
5. 새 endpoint를 게시하고 peer가 location reconcile을 수행한다.
6. 새 runtime이 readiness를 true로 바꾼다.

동시에 반환된 slot 수만큼 여러 replacement가 진행될 수 있으므로 항상 하나씩 직렬화되는 것은
아니다. 그러나 rollout 처리량과 일시적 unavailable 수는 drain, slot 반환과 reconcile 지연에
제한된다. 무중단 surge 여유가 필요하면 정상 replica 수보다 큰 `slotCount`를 사용해야 하며,
그 경우 새 runtime이 기존과 다른 논리 RID를 받는다는 점을 application topology가 허용해야 한다.

## 10. store 목표 계약

기존 `IZLinkLocationStore`의 peer/spot/actor/route row를 임의 slot 저장소로 재사용하지 않는다.
특히 framework route row는 application이 임의 key-value 용도로 쓰는 표면이 아니다.

framework는 routing id slot allocation capability interface와 결과 값 타입을 제공한다. 저장소 제품별
구현은 extension package가 제공한다. `IZLinkLocationStore`에 allocation member를 직접 추가하지
않는다. 자동 할당은 선택 기능이므로 기존 사용자 store 구현체에 새 필수 member를 추가하면
source compatibility가 깨지기 때문이다.

routing id slot allocation capability는 별도 store로 등록하지 않는다.
`AddLocationStore(instance)`로 등록한
**같은 인스턴스**가 location store와 routing id slot allocation capability를 함께 구현해야 한다.
framework는 등록된 인스턴스의 capability를 확인한다.

| 등록 인스턴스 | 자동 할당 설정 | 결과 |
|---------------|----------------|------|
| location + slot allocation 구현 | 사용 | routing id slot allocation capability 사용 |
| location만 구현 | 미사용 | 기존 location 기능 사용 |
| location만 구현 | 사용 | startup configuration error |

별도의 `AddRoutingIdSlotAllocationStore(...)`는 제공하지 않는다. slot, owner lease와 peer identity
정책을 같은 물리 저장소의 원자성 경계에서 처리해야 하기 때문이다.

아래는 언어 중립 의사 계약이다.

```text
AcquireRoutingIdSlot(groupName, members, slotCount, ownerId, leaseTtl)
    -> Acquired(slot, ownerToken, leaseExpiresAt, storeNow)
     | GroupExhausted
     | GroupConfigurationMismatch(expected, actual)
     | IdentityModeConflict

ReleaseRoutingIdSlot(groupName, slot, ownerToken)
    -> Released | IgnoredStale

ListRoutingIdSlots(groupName)
    -> slot allocation snapshot + storeNow
```

`AcquireRoutingIdSlot`은 해당 owner의 첫 acquire이면 owner lease까지 같은 원자 연산으로 기록한다.
같은 owner가 다른 group을 acquire하면 기존 lease를 확인하고 갱신한다. 기존 owner lease의
`nodeRid` 하나로 여러 할당을 대표하지 않으며, group별 slot과 generation은 slot row에 둔다.
각 routing id는 framework가 group member의 prefix와 반환된 slot으로 만든다.
heartbeat는 runtime당 하나의 owner lease 갱신 경로를 재사용한다.

`ListRoutingIdSlots`는 운영 조회용이다. application startup이 이 목록을 읽어 slot을 직접 선택하는
용도로 사용하지 않는다.

### 10.1 원자성 책임

`AcquireRoutingIdSlot`의 원자성은 capability를 구현하는 store가 보장한다. framework가 목록을
읽고 번호를 선택한 뒤 별도 write를 호출하는 방식은 허용하지 않는다. store 구현체는 빈 slot
선택부터 최초 owner lease 기록까지 하나의 transaction, compare-and-set 또는 같은 수준의
원자 연산으로 처리한다.

framework runtime은 확보한 routing id를 bind 전에 적용하고, lease를 갱신하며, socket을 닫은
뒤 slot을 반환하는 lifecycle을 책임진다. store의 할당 원자성과 framework의 lifecycle 책임은
서로 대체되지 않는다.

### 10.2 store 구현체의 semantic floor

routing id slot allocation capability를 구현한다는 것은 interface shape만 맞춘다는 뜻이 아니다.
구현체는 다음
최소 의미를 모두 보장하고, 사용하는 저장소 구성과 보장 범위를 구현체 문서에 명시해야 한다.

1. **논리 만료 판정**: slot owner의 만료는 row가 물리적으로 삭제됐는지가 아니라 acquire의
   store 기준 현재 시각이 `LeaseExpiresAt` 이상인지로 판정한다. Redis key TTL, MongoDB TTL
   index 같은 background 삭제는 정리 최적화일 뿐 소유권 판정 근거가 아니다.
2. **같은 원자성 경계의 store 시각**: `StoreNow`는 빈 slot 선택과 owner 확정에 사용한 것과 같은
   원자 연산 또는 같은 linearizable transaction snapshot에서 읽는다. application node의 wall
   clock이나 원자 연산 전후에 별도로 읽은 시각을 사용하지 않는다. 구현체 문서는 database server
   time, transaction timestamp 등 실제 시각 출처를 밝힌다.
3. **group별 linearizable acquire**: 같은 group에 대한 동시 acquire는 전체 호출에 하나의 순서가
   있는 것처럼 관찰되어야 하며, 유효한 slot 하나는 정확히 한 owner에게만 성공한다.
4. **확정 write의 failover durability**: 구현체가 성공으로 반환한 acquire는 그 구현체가 지원한다고
   밝힌 자동 failover 뒤에도 사라지지 않아야 한다. 성공한 claim이 사라질 수 있는 async replica
   failover 구성은 strict single-active semantic floor를 충족하지 않는다.
5. **원인 보존**: consistency나 durability 조건을 확인할 수 없으면 임의 성공이나 경합 결과로
   바꾸지 않고 infrastructure error를 반환한다.

일반 contract test는 모든 저장소 제품의 failover durability를 증명할 수 없다. 따라서 extension은
지원 topology, write acknowledgment, failover 시 보장과 제한을 문서로 제공해야 한다. 별도의
product-specific fault-injection test가 있더라도 이 문서 보증 요건을 대체하지 않는다. semantic
floor를 충족하지 않는 store는 개발·test 용도로 등록할 수는 있어도 strict single-active 운영
구성으로 표시할 수 없다.

### 10.3 Redis extension

공식 Redis extension은 acquire의 다음 작업을 하나의 Lua script 또는 같은 수준의 원자 연산으로
처리해야 한다.

- group metadata의 member channel·prefix 목록과 `slotCount` 확인 또는 최초 생성
- 모든 group member의 identity mode와 active fixed peer 부재 확인
- 같은 owner의 기존 slot 확인
- 만료 owner를 제외하고 가장 작은 빈 slot 선택
- slot generation 증가
- slot row와 owner index 기록
- owner lease 기록과 TTL 설정
- store 시각과 확정 결과 반환

Redis의 native key TTL은 만료 row 정리에만 사용한다. Lua script는 Redis server time과 저장된
`LeaseExpiresAt`을 비교해 논리 만료를 판정해야 한다. 공식 extension 문서는 standalone,
Sentinel, Cluster 등 지원 topology별 write 확정과 failover 보장을 구분해야 한다. 성공 응답 뒤
write 유실이 가능한 비동기 replica 자동 failover 구성은 strict single-active 운영 구성으로
지원한다고 표시하지 않는다.

Redis key와 row JSON의 byte 단위 형식은 정식 spec 승격 시
[Redis location store](../../spec/server/41-location-store-redis.ko.md)와 공용 fixture에 고정한다.

공식 Redis 구현체는 기존 location store와 routing id slot allocation capability를 같은 객체에서
제공한다.
사용자는 Redis 연결과 key prefix를 한 번만 설정한다.

```csharp
options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
    .SetConnectionString(topology.RedisEndpoint)
    .SetKeyPrefix(topology.RedisKeyPrefix)));

options.AddSpotMesh("zone")
    // 위 Redis 인스턴스가 제공하는 routing id slot allocation capability를 사용한다.
    .UseAllocatedRoutingId(slotCount: 100)
    .EnableRouter("tcp://0.0.0.0:9001");
```

in-memory location store도 capability를 구현하지만 단일 process 개발·contract test 용도다.
여러 process의 중복 할당 방지를 검증하거나 운영 배포에 사용하지 않는다.

### 10.4 공통 값 모델

언어별 interface는 다음 의미를 같은 수준으로 표현한다.

| 값 | field와 의미 |
|----|-------------|
| acquire request | group name, member channel·prefix 목록, slot count, owner id, lease TTL |
| acquired allocation | slot, owner token, lease expiry, store time |
| group exhausted | 현재 범위에 할당 가능한 slot 없음 |
| configuration mismatch | store에 고정된 member 구성·count와 요청값이 다름 |
| identity mode conflict | 같은 group member에서 fixed와 allocated 정책이 충돌 |
| release result | `Released` 또는 이전 owner token을 뜻하는 `IgnoredStale` |
| snapshot | group 설정, 현재 slot 목록과 store time |

store 연결 실패, timeout과 serialization 실패는 acquire 결과 variant로 바꾸지 않고 원인을 보존한
infrastructure error로 전달한다.

### 10.5 언어별 interface

아래 interface는 이 초안의 목표 public surface다. 정식 계약으로 승격할 때 각 언어의
`server/languages/<lang>/` spec에 옮기고 실제 package export와 contract test로 고정한다.
store는 최종 routing id 문자열이 아니라 정수 slot을 할당하므로 capability와 operation 이름은
모든 언어에서 `RoutingIdSlot`로 통일한다. 최종 routing id는 framework가 member prefix와 slot을
결합해 만든다.
현재 언어 구현에 특정 builder의 직접 routing id 설정 표면이 없다면 자동 할당 메서드만 먼저
추가하지 않는다. 공통 계약에서 직접 지정과 자동 할당을 함께 확정한 뒤 두 방식을 모두 제공한다.

#### 10.5.1 .NET

`.NET`은 `ValueTask`와 `CancellationToken`을 사용한다. acquire 결과는 nullable field 조합이
아니라 닫힌 record hierarchy로 표현한다.

기존 `ZLinkLocationOptions`에는 다음 두 시간 옵션을 추가한다. 기존 public member는 유지한다.

```csharp
public sealed class ZLinkLocationOptions
{
    public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(10);
    public TimeSpan OwnerLeaseTtl { get; set; } = TimeSpan.FromSeconds(30);
    public TimeSpan PollingInterval { get; set; } = TimeSpan.FromSeconds(1);
    public int ListPageSize { get; set; } = 1000;
    public TimeSpan StoreFailureGrace { get; set; } = TimeSpan.FromSeconds(30);

    public TimeSpan RoutingIdFencingMargin { get; set; }
        = TimeSpan.FromSeconds(5);

    public TimeSpan OwnerLeaseRenewTimeout { get; set; }
        = TimeSpan.FromSeconds(3);

    public void MapSpotMeshToRouteChannel(
        string spotMeshName,
        string routeChannelName);
}
```

```csharp
public interface IZLinkAllocatedRoutingIdProvider
{
    ValueTask<ZLinkAllocatedRoutingId> WaitForReadyAllocationAsync(
        string groupName,
        CancellationToken cancellationToken = default);
}

public sealed record ZLinkAllocatedRoutingId(
    string GroupName,
    int Slot,
    IReadOnlyDictionary<string, RoutingId> MemberRoutingIds);
```

```csharp
public interface IZLinkRoutingIdSlotAllocationStore
{
    ValueTask<ZLinkRoutingIdSlotAcquireResult> AcquireRoutingIdSlotAsync(
        ZLinkRoutingIdSlotAcquireRequest request,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRoutingIdSlotReleaseResult> ReleaseRoutingIdSlotAsync(
        string groupName,
        int slot,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRoutingIdSlotAllocationSnapshot> ListRoutingIdSlotsAsync(
        string groupName,
        CancellationToken cancellationToken = default);
}

public sealed record ZLinkRoutingIdSlotAcquireRequest(
    string GroupName,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> Members,
    int SlotCount,
    string OwnerId,
    TimeSpan LeaseTtl);

public abstract record ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotAcquired(ZLinkRoutingIdSlotAllocation Allocation)
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupExhausted
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotGroupConfigurationMismatch(
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ExpectedMembers,
    int ExpectedSlotCount,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> ActualMembers,
    int ActualSlotCount)
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotIdentityModeConflict
    : ZLinkRoutingIdSlotAcquireResult;

public sealed record ZLinkRoutingIdSlotAllocation(
    int Slot,
    ZLinkLocationOwnerToken Owner,
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset StoreNow);

public enum ZLinkRoutingIdSlotReleaseResult
{
    Released = 1,
    IgnoredStale = 2
}

public sealed record ZLinkRoutingIdSlotAllocationSnapshot(
    string GroupName,
    IReadOnlyList<ZLinkRoutingIdSlotAllocationMember> Members,
    int SlotCount,
    IReadOnlyList<ZLinkRoutingIdSlotAllocation> Allocations,
    DateTimeOffset StoreNow);

public sealed record ZLinkRoutingIdSlotAllocationMember(
    string ChannelName,
    string RoutingIdPrefix);
```

```csharp
public interface IZLinkClientServerChannelBuilder
{
    IZLinkClientServerChannelBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkClientServerChannelBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkClientServerChannelBuilder SetRoutingIdAllocationGroup(string groupName);
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkFanoutChannelBuilder SetRoutingIdAllocationGroup(string groupName);
}

public interface IZLinkRouteMeshChannelBuilder
{
    IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(int slotCount);
    IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkRouteMeshChannelBuilder SetRoutingIdAllocationGroup(string groupName);
}

public interface IZLinkSpotNodeBuilder
{
    IZLinkSpotNodeBuilder UseAllocatedRoutingId(int slotCount);

    IZLinkSpotNodeBuilder UseAllocatedRoutingId(
        int slotCount,
        string routingIdPrefix);
    IZLinkSpotNodeBuilder SetRoutingIdAllocationGroup(string groupName);
}

public sealed class ZLinkRedisLocationStore :
    IZLinkLocationStore,
    IZLinkRoutingIdSlotAllocationStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    // 기존 constructor와 public member를 유지한다.
}
```

#### 10.5.2 Java

Java는 `CompletionStage`와 sealed interface/record를 사용한다. infrastructure error는 실패한
`CompletionStage`로 전달한다.

기존 `ZLinkLocationOptions`에는 다음 getter와 setter를 추가한다. setter는 기존 시간 옵션과 같이
0 이하의 `Duration`을 거부한다.

```java
public final class ZLinkLocationOptions {
    public Duration heartbeatInterval();
    public void setHeartbeatInterval(Duration value);

    public Duration ownerLeaseTtl();
    public void setOwnerLeaseTtl(Duration value);

    public Duration routingIdFencingMargin();
    public void setRoutingIdFencingMargin(Duration value);

    public Duration ownerLeaseRenewTimeout();
    public void setOwnerLeaseRenewTimeout(Duration value);

    // polling, page size, store failure와 Spot-route 매핑의 기존 public member는 유지한다.
}
```

새 객체는 heartbeat 10초, owner lease TTL 30초, routing id fencing margin 5초와 owner lease
renew timeout 3초를 사용한다.

```java
public interface ZLinkAllocatedRoutingIdProvider {
    CompletionStage<ZLinkAllocatedRoutingId> waitForReadyAllocation(String groupName);
}

public record ZLinkAllocatedRoutingId(
    String groupName,
    int slot,
    Map<String, RoutingId> memberRoutingIds) {}
```

```java
public interface ZLinkRoutingIdSlotAllocationStore {
    CompletionStage<ZLinkRoutingIdSlotAcquireResult> acquireRoutingIdSlot(
        ZLinkRoutingIdSlotAcquireRequest request);

    CompletionStage<ZLinkRoutingIdSlotReleaseResult> releaseRoutingIdSlot(
        String groupName,
        int slot,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkRoutingIdSlotAllocationSnapshot> listRoutingIdSlots(
        String groupName);
}

public record ZLinkRoutingIdSlotAcquireRequest(
    String groupName,
    List<ZLinkRoutingIdSlotAllocationMember> members,
    int slotCount,
    String ownerId,
    Duration leaseTtl) {}

public sealed interface ZLinkRoutingIdSlotAcquireResult permits
    ZLinkRoutingIdSlotAcquired,
    ZLinkRoutingIdSlotGroupExhausted,
    ZLinkRoutingIdSlotGroupConfigurationMismatch,
    ZLinkRoutingIdSlotIdentityModeConflict {}

public record ZLinkRoutingIdSlotAcquired(ZLinkRoutingIdSlotAllocation allocation)
    implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotGroupExhausted()
    implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotGroupConfigurationMismatch(
    List<ZLinkRoutingIdSlotAllocationMember> expectedMembers,
    int expectedSlotCount,
    List<ZLinkRoutingIdSlotAllocationMember> actualMembers,
    int actualSlotCount) implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotIdentityModeConflict()
    implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotAllocation(
    int slot,
    ZLinkLocationOwnerToken owner,
    Instant leaseExpiresAt,
    Instant storeNow) {}

public enum ZLinkRoutingIdSlotReleaseResult {
    RELEASED(1),
    IGNORED_STALE(2);

    private final int value;

    ZLinkRoutingIdSlotReleaseResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }
}

public record ZLinkRoutingIdSlotAllocationSnapshot(
    String groupName,
    List<ZLinkRoutingIdSlotAllocationMember> members,
    int slotCount,
    List<ZLinkRoutingIdSlotAllocation> allocations,
    Instant storeNow) {}

public record ZLinkRoutingIdSlotAllocationMember(
    String channelName,
    String routingIdPrefix) {}
```

```java
public interface ClientServerChannelBuilder {
    ClientServerChannelBuilder useAllocatedRoutingId(int slotCount);
    ClientServerChannelBuilder useAllocatedRoutingId(
        int slotCount,
        String routingIdPrefix);
    ClientServerChannelBuilder setRoutingIdAllocationGroup(String groupName);
}

public interface FanoutChannelBuilder {
    FanoutChannelBuilder useAllocatedRoutingId(int slotCount);
    FanoutChannelBuilder useAllocatedRoutingId(int slotCount, String routingIdPrefix);
    FanoutChannelBuilder setRoutingIdAllocationGroup(String groupName);
}

public interface RouteMeshChannelBuilder {
    RouteMeshChannelBuilder useAllocatedRoutingId(int slotCount);
    RouteMeshChannelBuilder useAllocatedRoutingId(int slotCount, String routingIdPrefix);
    RouteMeshChannelBuilder setRoutingIdAllocationGroup(String groupName);
}

public interface ZLinkSpotNodeBuilder {
    ZLinkSpotNodeBuilder useAllocatedRoutingId(int slotCount);
    ZLinkSpotNodeBuilder useAllocatedRoutingId(int slotCount, String routingIdPrefix);
    ZLinkSpotNodeBuilder setRoutingIdAllocationGroup(String groupName);
}

public final class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    ZLinkRoutingIdSlotAllocationStore,
    ZLinkLocationChangeStampStore,
    AutoCloseable {
    // 기존 constructor와 public member를 유지한다.
}
```

#### 10.5.3 Kotlin

Kotlin은 Java runtime과 location store implementation을 공유한다. 따라서 저장소가 구현하는
정식 capability는 Java의 `ZLinkRoutingIdSlotAllocationStore` 하나이며 같은 의미의 별도 nominal
interface를 중복해서 만들지 않는다. Kotlin에서 사용자 store를 구현할 때는 suspending adapter를
제공한다.

lease 시간도 Java의 `ZLinkLocationOptions`를 그대로 사용한다. Kotlin 전용 option type이나
별도 값을 만들지 않는다.

할당 결과 조회도 Java의 `ZLinkAllocatedRoutingIdProvider`와 `ZLinkAllocatedRoutingId`를 그대로
사용한다. Kotlin 전용 nominal type은 추가하지 않으며 `CompletionStage.await()`로 기다린다.

```kotlin
val locations = options.configureLocations()
locations.setHeartbeatInterval(Duration.ofSeconds(10)) // owner lease 갱신 주기
locations.setOwnerLeaseTtl(Duration.ofSeconds(30)) // owner lease 유효 시간
locations.setRoutingIdFencingMargin(Duration.ofSeconds(5)) // 만료 전 종료 여유 시간
locations.setOwnerLeaseRenewTimeout(Duration.ofSeconds(3)) // renew 응답 대기 상한
```

```kotlin
abstract class ZLinkSuspendingAllocatingLocationStore :
    ZLinkSuspendingLocationStore(),
    ZLinkRoutingIdSlotAllocationStore {

    protected abstract suspend fun acquireRoutingIdSlotSuspending(
        request: ZLinkRoutingIdSlotAcquireRequest,
    ): ZLinkRoutingIdSlotAcquireResult

    protected abstract suspend fun releaseRoutingIdSlotSuspending(
        groupName: String,
        slot: Int,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkRoutingIdSlotReleaseResult

    protected abstract suspend fun listRoutingIdSlotsSuspending(
        groupName: String,
    ): ZLinkRoutingIdSlotAllocationSnapshot

    // CompletionStage bridge는 base class가 제공한다.
}
```

```kotlin
interface ClientServerChannelBuilder {
    fun useAllocatedRoutingId(slotCount: Int): ClientServerChannelBuilder
    fun useAllocatedRoutingId(
        slotCount: Int,
        routingIdPrefix: String,
    ): ClientServerChannelBuilder
    fun setRoutingIdAllocationGroup(groupName: String): ClientServerChannelBuilder
}

interface FanoutChannelBuilder {
    fun useAllocatedRoutingId(slotCount: Int): FanoutChannelBuilder
    fun useAllocatedRoutingId(
        slotCount: Int,
        routingIdPrefix: String,
    ): FanoutChannelBuilder
    fun setRoutingIdAllocationGroup(groupName: String): FanoutChannelBuilder
}

interface RouteMeshChannelBuilder {
    fun useAllocatedRoutingId(slotCount: Int): RouteMeshChannelBuilder
    fun useAllocatedRoutingId(
        slotCount: Int,
        routingIdPrefix: String,
    ): RouteMeshChannelBuilder
    fun setRoutingIdAllocationGroup(groupName: String): RouteMeshChannelBuilder
}

interface ZLinkSpotNodeBuilder {
    fun useAllocatedRoutingId(slotCount: Int): ZLinkSpotNodeBuilder

    fun useAllocatedRoutingId(
        slotCount: Int,
        routingIdPrefix: String,
    ): ZLinkSpotNodeBuilder
    fun setRoutingIdAllocationGroup(groupName: String): ZLinkSpotNodeBuilder
}
```

공식 Redis store는 Java 구현을 Kotlin에서도 그대로 등록한다. Redis 연결이나 key prefix를
Kotlin용으로 다시 설정하는 두 번째 store를 만들지 않는다.

#### 10.5.4 Node.js

Node.js는 `Promise`, optional `AbortSignal`과 discriminated union을 사용한다. 날짜는 기존
location contract와 같이 `Date`로 표현한다.

기존 `ZLinkLocationOptions`에 millisecond 단위의 두 값을 추가한다. 생략한 값은
`zlinkDefaultLocationOptions`의 기본값을 사용한다.

```ts
export interface ZLinkLocationOptions {
    readonly heartbeatIntervalMs?: number;
    readonly ownerLeaseTtlMs?: number;
    readonly pollingIntervalMs?: number;
    readonly listPageSize?: number;
    readonly storeFailureGraceMs?: number;
    readonly routingIdFencingMarginMs?: number;
    readonly ownerLeaseRenewTimeoutMs?: number;
}

export declare const zlinkDefaultLocationOptions: Required<ZLinkLocationOptions>;
```

`zlinkDefaultLocationOptions.routingIdFencingMarginMs`는 `5000`,
`zlinkDefaultLocationOptions.ownerLeaseRenewTimeoutMs`는 `3000`이다. 기존 heartbeat와 TTL의
기본값도 각각 `10000`, `30000`으로 변경한다.

```ts
export interface ZLinkAllocatedRoutingIdProvider {
    waitForReadyAllocation(
        groupName: string,
        signal?: AbortSignal,
    ): Promise<ZLinkAllocatedRoutingId>;
}

export interface ZLinkAllocatedRoutingId {
    readonly groupName: string;
    readonly slot: number;
    readonly memberRoutingIds: Readonly<Record<string, RoutingId>>;
}
```

```ts
export interface ZLinkRoutingIdSlotAllocationStore {
    acquireRoutingIdSlot(
        request: ZLinkRoutingIdSlotAcquireRequest,
        signal?: AbortSignal,
    ): Promise<ZLinkRoutingIdSlotAcquireResult>;

    releaseRoutingIdSlot(
        groupName: string,
        slot: number,
        owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal,
    ): Promise<ZLinkRoutingIdSlotReleaseResult>;

    listRoutingIdSlots(
        groupName: string,
        signal?: AbortSignal,
    ): Promise<ZLinkRoutingIdSlotAllocationSnapshot>;
}

export interface ZLinkRoutingIdSlotAcquireRequest {
    readonly groupName: string;
    readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
    readonly slotCount: number;
    readonly ownerId: string;
    readonly leaseTtlMs: number;
}

export type ZLinkRoutingIdSlotAcquireResult =
    | { readonly kind: 'acquired'; readonly allocation: ZLinkRoutingIdSlotAllocation }
    | { readonly kind: 'group-exhausted' }
    | {
          readonly kind: 'group-configuration-mismatch';
          readonly expectedMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
          readonly expectedSlotCount: number;
          readonly actualMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
          readonly actualSlotCount: number;
      }
    | { readonly kind: 'identity-mode-conflict' };

export interface ZLinkRoutingIdSlotAllocation {
    readonly slot: number;
    readonly owner: ZLinkLocationOwnerToken;
    readonly leaseExpiresAt: Date;
    readonly storeNow: Date;
}

export type ZLinkRoutingIdSlotReleaseResult = 'released' | 'ignored-stale';

export interface ZLinkRoutingIdSlotAllocationSnapshot {
    readonly groupName: string;
    readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
    readonly slotCount: number;
    readonly allocations: readonly ZLinkRoutingIdSlotAllocation[];
    readonly storeNow: Date;
}

export interface ZLinkRoutingIdSlotAllocationMember {
    readonly channelName: string;
    readonly routingIdPrefix: string;
}
```

```ts
export interface ZLinkClientServerChannelBuilder {
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    routingIdAllocationGroup(groupName: string): this;
}

export interface ZLinkFanoutChannelBuilder {
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    routingIdAllocationGroup(groupName: string): this;
}

export interface ZLinkRouteMeshChannelBuilder {
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    routingIdAllocationGroup(groupName: string): this;
}

export interface ZLinkSpotNodeBuilder {
    useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
    routingIdAllocationGroup(groupName: string): this;
}

export class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    ZLinkRoutingIdSlotAllocationStore,
    ZLinkLocationChangeStampStore {
    // 기존 constructor와 public member를 유지한다.
}
```

#### 10.5.5 C++

C++는 `task_t`, value type과 `std::variant`를 사용한다. cancellation 표현은 C++ framework의
기존 `task_t` 실행 계약을 따른다.

기존 `location_options_t`에 다음 두 field를 추가한다.

```cpp
struct location_options_t
{
    std::chrono::milliseconds heartbeat_interval{10000};
    std::chrono::milliseconds owner_lease_ttl{30000};
    std::chrono::milliseconds polling_interval{1000};
    int list_page_size = 1000;
    std::chrono::milliseconds store_failure_grace{30000};
    std::chrono::milliseconds routing_id_fencing_margin{5000};
    std::chrono::milliseconds owner_lease_renew_timeout{3000};
    std::map<std::string, std::string> spot_router_channels;
};
```

```cpp
struct allocated_routing_id_t
{
    std::string group_name;
    std::uint32_t slot;
    std::map<std::string, routing_id_t> member_routing_ids;
};

class allocated_routing_id_provider_t
{
public:
    virtual task_t<allocated_routing_id_t> wait_for_ready_allocation (
        std::string group_name) = 0;
};
```

```cpp
struct routing_id_slot_allocation_member_t
{
    std::string channel_name;
    std::string routing_id_prefix;
};

struct routing_id_slot_acquire_request_t
{
    std::string group_name;
    std::vector<routing_id_slot_allocation_member_t> members;
    std::uint32_t slot_count;
    std::string owner_id;
    std::chrono::milliseconds lease_ttl;
};

struct routing_id_slot_allocation_t
{
    std::uint32_t slot;
    location_owner_token_t owner;
    std::chrono::system_clock::time_point lease_expires_at;
    std::chrono::system_clock::time_point store_now;
};

struct routing_id_slot_acquired_t { routing_id_slot_allocation_t allocation; };
struct routing_id_slot_group_exhausted_t {};
struct routing_id_slot_group_configuration_mismatch_t
{
    std::vector<routing_id_slot_allocation_member_t> expected_members;
    std::uint32_t expected_slot_count;
    std::vector<routing_id_slot_allocation_member_t> actual_members;
    std::uint32_t actual_slot_count;
};
struct routing_id_slot_identity_mode_conflict_t {};

using routing_id_slot_acquire_result_t = std::variant<
    routing_id_slot_acquired_t,
    routing_id_slot_group_exhausted_t,
    routing_id_slot_group_configuration_mismatch_t,
    routing_id_slot_identity_mode_conflict_t>;

enum class routing_id_slot_release_result_t
{
    released = 1,
    ignored_stale = 2
};

struct routing_id_slot_allocation_snapshot_t
{
    std::string group_name;
    std::vector<routing_id_slot_allocation_member_t> members;
    std::uint32_t slot_count;
    std::vector<routing_id_slot_allocation_t> allocations;
    std::chrono::system_clock::time_point store_now;
};

class routing_id_slot_allocation_store_t
{
public:
    virtual task_t<routing_id_slot_acquire_result_t>
        acquire_routing_id_slot (routing_id_slot_acquire_request_t request) = 0;

    virtual task_t<routing_id_slot_release_result_t>
        release_routing_id_slot (std::string group_name,
                         std::uint32_t slot,
                         location_owner_token_t owner) = 0;

    virtual task_t<routing_id_slot_allocation_snapshot_t>
        list_routing_id_slots (std::string group_name) = 0;
};
```

```cpp
class client_server_channel_builder_t
{
public:
    client_server_channel_builder_t &use_allocated_routing_id (
        std::uint32_t slot_count);
    client_server_channel_builder_t &use_allocated_routing_id (
        std::uint32_t slot_count,
        std::string routing_id_prefix);
    client_server_channel_builder_t &set_routing_id_allocation_group (
        std::string group_name);
};

class fanout_channel_builder_t
{
public:
    fanout_channel_builder_t &use_allocated_routing_id (std::uint32_t slot_count);
    fanout_channel_builder_t &use_allocated_routing_id (
        std::uint32_t slot_count,
        std::string routing_id_prefix);
    fanout_channel_builder_t &set_routing_id_allocation_group (
        std::string group_name);
};

class route_mesh_channel_builder_t
{
public:
    route_mesh_channel_builder_t &use_allocated_routing_id (std::uint32_t slot_count);
    route_mesh_channel_builder_t &use_allocated_routing_id (
        std::uint32_t slot_count,
        std::string routing_id_prefix);
    route_mesh_channel_builder_t &set_routing_id_allocation_group (
        std::string group_name);
};

class spot_node_builder_t
{
public:
    spot_node_builder_t &use_allocated_routing_id (std::uint32_t slot_count);
    spot_node_builder_t &use_allocated_routing_id (
        std::uint32_t slot_count,
        std::string routing_id_prefix);
    spot_node_builder_t &set_routing_id_allocation_group (std::string group_name);
};

class redis_location_store_t : public location_store_t,
                               public routing_id_slot_allocation_store_t,
                               public location_change_stamp_store_t
{
    // Redis 구현
};
```

#### 10.5.6 등록 규칙

각 언어에서 Redis location store는 기존 `addLocationStore` 계열 API로 한 번만 등록한다.
framework는 같은 객체가 routing id slot allocation capability도 구현하는지
runtime type/capability 검사로
확인한다. reflection으로 private member를 조회하거나 별도 adapter store를 application에
요구하지 않는다.

lease 시간은 같은 등록 호출의 Redis option에 복사하지 않는다. framework는
`ConfigureLocations()`에 설정된 `OwnerLeaseTtl`을 acquire request의 `LeaseTtl`로 전달하고,
`HeartbeatInterval`, `RoutingIdFencingMargin`과 `OwnerLeaseRenewTimeout`을 runtime의 갱신 및
self-fencing 판단에 사용한다. 따라서 Redis와 사용자 store가 별도의 시간 설정 API를 제공할
필요는 없다.

할당 결과 provider는 framework runtime service로 등록한다. application은 location store를 직접
조회하거나 slot row를 파싱하지 않는다. provider는 runtime이 `Ready`인 allocation만 반환하며
release와 renew operation은 제공하지 않는다.

사용자 store가 routing id slot allocation capability를 구현하면 acquire의 원자성은 그 구현체의
책임이다.
framework contract test kit는 같은 store 인스턴스를 대상으로 §13.1의 동시 acquire,
generation과 stale release 시나리오를 실행할 수 있어야 한다.

## 11. 관측과 운영 조회

자동 할당 runtime은 최소한 다음 상태를 운영 조회와 location runtime event로 구분해야 한다.

| 상태 | 의미 |
|------|------|
| `WaitingForSlot` | allocation group이 가득 차 socket을 bind하지 않고 대기 중 |
| `Allocated` | slot을 확보했지만 startup이 아직 완료되지 않음 |
| `Ready` | routing id 적용, bind와 location 게시 완료 |
| `LeaseAtRisk` | store 장애로 다음 lease 갱신을 확인하지 못함 |
| `Quiescing` | lease 안전 기한 전에 서비스 제공을 중단하는 중 |
| `Released` | 정상 종료 과정에서 slot 반환 완료 |
| `Fenced` | lease 안전성을 잃어 socket을 강제로 닫고 host 종료를 시작한 terminal 상태 |

정상 종료는 `Ready → Quiescing → Released`로 전이한다. lease 갱신을 안전 기한까지 확인하지
못한 첫 renew 실패에서는 `Ready → LeaseAtRisk`로 전이한다. `fenceDeadline` 전에 renew가
성공하면 `LeaseAtRisk → Ready`로 복귀하고 기존 socket과 readiness를 유지한다. deadline까지
renew 성공을 확인하지 못한 경우에만 `LeaseAtRisk → Quiescing → Fenced`로 전이하고 framework가
host 종료를 요청한다. `Fenced` runtime은 같은 process에서 slot을 다시 acquire하지 않는다.
group 구성 불일치와 잘못된 option은 runtime 상태를 만들기 전의 fatal startup error다.

운영 조회는 group name, member channel과 routing id, slot count, 현재 slot, generation, lease
만료까지 남은 시간과 readiness를 제공한다. owner id 전체 값은 진단에 필요할 때만 노출하며
metric label로 사용하지 않는다.

기본 metric은 다음과 같다.

- group별 allocated slot 수와 capacity
- allocation 대기 runtime 수
- acquire conflict와 group 소진 횟수
- lease 갱신 실패와 lease 안전 종료 횟수

## 12. 오류와 결과

| 상황 | 결과 |
|------|------|
| location store 미등록 | configuration error |
| 등록한 location store가 routing id slot allocation capability를 구현하지 않음 | 자동 할당 사용 시 startup configuration error |
| store가 §10.2 semantic floor를 보증하지 않는 topology | strict single-active 운영 구성으로 지원하지 않음 |
| `SetRoutingId`와 자동 할당 동시 설정 | configuration error |
| 같은 group member에서 고정 RID와 자동 할당 혼합 | 지원하지 않는 배포 구성, startup error |
| 빈 group name·prefix, `slotCount < 1` | validation error |
| 생성 routing id가 255 bytes 초과 | validation error |
| lease 시간 값이 0 이하이거나 §8.4 관계식 위반 | store acquire 전 configuration error |
| 같은 group의 member 구성, prefix 또는 slot count 불일치 | startup error, 자동 재시도하지 않음 |
| group 소진 | readiness false로 대기, slot을 임의 생성하지 않음 |
| acquire 중 store 장애 | readiness false, 같은 요청을 멱등 재시도 |
| 실행 중 lease renew 한 번 실패 | `LeaseAtRisk`, 기존 socket과 readiness 유지 |
| `fenceDeadline` 전 renew 성공 | `LeaseAtRisk → Ready`, 기존 서비스 계속 제공 |
| `fenceDeadline`까지 renew 성공 미확인 | `LeaseAtRisk → Quiescing → Fenced`, socket과 host 종료 |
| 이전 generation의 release/renew | `IgnoredStale`, 현재 owner 상태 유지 |
| bind 실패 | 확보한 slot 정리 시도 후 startup error |

## 13. 회귀 및 contract test

### 13.1 공통 store contract test

- 빈 `zone`, count 100에 순차 acquire하면 slot 1, 2, 3, 4가 반환된다.
- slot 2를 release한 뒤 acquire하면 slot 2가 반환된다.
- 100개 동시 acquire는 서로 다른 1..100을 정확히 한 번씩 반환한다.
- 101번째 acquire는 `GroupExhausted`다.
- 같은 owner의 acquire 재시도는 같은 slot과 generation을 반환한다.
- 다른 owner가 유효 lease의 slot을 claim할 수 없다.
- owner lease 만료 뒤 새 owner가 같은 slot과 증가한 generation을 받는다.
- 물리 row가 아직 삭제되지 않았어도 `StoreNow >= LeaseExpiresAt`이면 해당 slot을 만료로 판정한다.
- 이전 owner token의 늦은 release가 새 owner의 slot을 지우지 않는다.
- 같은 group에 다른 member 구성, prefix 또는 slot count를 사용하면
  `GroupConfigurationMismatch`다.
- fixed identity가 활성인 member를 포함한 group의 acquire는 `IdentityModeConflict`다.
- acquire의 빈 slot 선택, generation 증가, slot row와 최초 owner lease 기록은 일부만 반영되지
  않고 모두 반영되거나 모두 반영되지 않는다.
- group이 다르면 같은 숫자 slot을 각각 할당할 수 있다.
- 같은 owner가 같은 group을 다시 acquire하면 member 수와 관계없이 slot 하나만 유지한다.

이 suite는 in-memory, 공식 Redis와 사용자 store contract kit에 동일하게 적용한다. process
내부 lock만 사용하는 in-memory test와 별도로 Redis test는 독립 client의 동시 acquire를 실행해
저장소 경계의 원자성을 검증한다.

contract test만으로 실제 product failover durability를 증명했다고 판정하지 않는다. 각 공식·사용자
extension은 §10.2의 지원 topology, 시각 출처, write acknowledgment와 성공한 acquire의 failover
보존 근거를 별도 conformance 문서로 제공해야 한다.

### 13.2 framework contract test

- location 옵션을 지정하지 않으면 heartbeat 10초, owner lease TTL 30초, routing id fencing margin
  5초와 owner lease renew timeout 3초를 사용한다.
- 네 시간 옵션은 기존 location runtime 설정 표면에서 변경할 수 있고, 설정한 TTL이 모든
  allocation group의 acquire request에 전달된다.
- 여러 allocation group을 사용하는 runtime은 같은 heartbeat, TTL, margin과 renew timeout을
  공유하며 group별 override API를 제공하지 않는다.
- 각 시간 값이 0 이하이거나 `H + R < T - M`을 만족하지 않으면 store acquire 전에
  configuration error로 실패한다.
- 자동 할당 RID가 없으면 새 fencing margin과 renew timeout이 기존 location runtime의 장애
  정책을 변경하지 않는다.
- 할당 결과 provider는 `Ready` 전에는 완료되지 않고, 완료 뒤 group의 slot과 모든 member routing
  id를 반환한다.
- 등록되지 않은 group 조회는 configuration error이고, `WaitingForSlot` group 조회는 할당 완료까지
  기다리며, `Fenced` runtime의 미완료 조회는 실패한다.
- 자동 RID를 쓰는 `SpotNode`의 entry spot은 같은 할당 RID를 기본으로 사용한다.
- 같은 `SpotNode` builder에서 자동 RID와 `SetEntrySpotRoutingId(...)`를 함께 설정하면 startup 전에
  실패한다.
- `UseAllocatedRoutingId(100)`은 channel 이름을 기본 group name과 routing id prefix로 사용한다.
- 기본 생성 형식은 구분자 없는 `<channelName><slot>`이다.
- routing id prefix override에 `zone-`를 주면 `zone-1`, `zone-2`를 생성한다.
- routing id prefix override에 `zone/`를 주면 `zone/1`, `zone/2`를 생성한다.
- prefix override는 store의 allocation group name을 바꾸지 않는다.
- 같은 group의 member 구성, prefix 또는 slot count가 다르면 configuration mismatch다.
- `AddLocationStore(...)`로 등록한 같은 인스턴스가 routing id slot allocation capability를
  제공하면 별도 store 등록 없이 사용한다.
- routing id slot allocation capability가 없는 사용자 location store에서 자동 할당을 설정하면
  startup 전에 실패한다.
- 자동 할당을 사용하지 않으면 routing id slot allocation capability가 없는 기존 사용자 store도
  그대로 동작한다.
- `SetRoutingId`와 자동 할당을 함께 설정하면 startup 전에 실패한다.
- 두 메서드의 호출 순서를 바꿔도 함께 설정한 결과는 동일하게 실패한다.
- allocation group만 설정하고 자동 할당을 설정하지 않으면 startup 전에 실패한다.
- allocation group과 자동 할당의 호출 순서를 바꿔도 같은 group 설정이 만들어진다.
- client/server, fanout, route mesh와 `SpotNode` builder가 모두 같은 자동 할당 설정을 제공한다.
- fanout 자동 RID는 publish 목적지 선택에 사용하지 않고 publisher/subscriber socket의 추적·진단
  identity로 적용한다.
- 같은 group member에서 고정 RID process와 자동 할당 process를 섞으면 startup이 실패한다.
- 같은 allocation group을 지정한 `spotnode`와 `routermesh`는 한 runtime에서 slot 하나를 공유해
  `spotnode1`과 `routermesh1`처럼 같은 번호를 사용한다.
- allocation group을 생략한 서로 다른 builder는 각 channel 이름의 독립된 group에서 slot을
  할당받는다.
- 한 runtime에서 여러 group을 사용하면 group마다 slot 하나를 받고, 모든 slot row는 같은 runtime
  owner id를 참조한다.
- 여러 group 중 하나가 소진되면 이미 확보한 다른 group의 slot을 반환하고 bind 없이 대기한다.
- 공유 group member 하나의 bind가 실패하면 나머지 member socket도 닫고 group slot을 반환한다.
- 할당된 routing id가 native bind 전에 적용된다.
- 첫 acquire 응답 직후 heartbeat가 시작되고, 첫 socket bind보다 앞선다.
- startup 중 renew를 안전하게 확인하지 못하면 socket을 bind하지 않고 확보한 slot을 정리한다.
- heartbeat, TTL, margin과 renew timeout이 §8.4 관계를 위반하면 startup 전에 실패한다.
- group 소진 runtime은 socket을 bind하지 않고 readiness false로 대기한다.
- bind 실패는 확보한 slot을 release하며, release 실패 시 lease 만료로 회수된다.
- 정상 종료는 socket을 닫은 뒤 slot을 반환한다.
- lease 안전 기한이 지나기 전에 이전 runtime의 socket이 닫힌다.
- lease 안전성을 잃은 runtime은 `Fenced`로 전이해 host 종료를 요청하고 같은 process에서 slot을
  다시 acquire하지 않는다.
- `LeaseAtRisk` 상태에서 `fenceDeadline` 전 renew가 성공하면 `Ready`로 복귀하고 socket을 닫지
  않는다.
- generation은 message wire metadata에 추가되지 않는다.

### 13.3 공통 E2E

1. 동일 설정의 zone server 네 개를 시작해 `zone1..4`를 확인한다.
2. `zone2`를 정상 종료하고 새 server가 `zone2`를 받는지 확인한다.
3. 한 server를 강제 종료하고 lease 만료 전에는 해당 slot이 재할당되지 않는지 확인한다.
4. lease 만료 뒤 새 server가 같은 routing id와 새 endpoint를 게시하는지 확인한다.
5. 기존 caller가 location reconcile 뒤 새 endpoint의 같은 routing id로 요청하는지 확인한다.
6. store 연결을 끊어 기존 owner가 lease 안전 기한 전에 readiness를 내리고 socket을 닫는지
   확인한다.
7. store를 복구해 하나의 owner만 해당 slot을 서비스하는지 확인한다.
8. `slotCount == replica count`인 surge replacement가 `WaitingForSlot`에 머물고, 이전 runtime의
   drain·socket close·release 뒤 같은 slot과 증가한 generation을 받는지 확인한다.
9. startup bind를 지연한 상태에서도 acquire 직후 heartbeat가 lease를 유지하는지 확인한다.
10. renew 한 번을 실패시킨 뒤 `fenceDeadline` 전에 store를 복구해 runtime이
    `LeaseAtRisk → Ready`로 돌아오고 기존 socket을 유지하는지 확인한다.

## 14. 정식 계약 승격과 구현 순서

> 2026-07-15 현재 공통 spec 네 문서와 `.NET` interface 문서에 계약을 반영했고, `.NET`의 builder,
> runtime, in-memory·Redis store와 contract test를 구현했다. 다른 언어와 공통 E2E는 아래 순서의
> 후속 작업이며, 이 초안의 다른 언어 시그니처는 아직 정식 계약이 아니다.

| 단계 | 작업 | 완료 기준 |
|------|------|-----------|
| 1. 설계 검토 | allocation group 의미, lease 안전 종료와 public builder 표면 확정 | 결정되지 않은 계약 항목 없음 |
| 2. 공통 spec | `10-channel-topology`, `21-spot-node`, `40-location-runtime`, `41-location-store-redis`에 목표 계약 반영 | **완료** — 공통 의미와 Redis 형식 고정 |
| 3. 언어별 spec | C++, .NET, Java/Kotlin, Node의 정확한 builder/store interface 고정 | **`.NET` 완료**, 다른 언어 후속 작업 |
| 4. gap 기록 | 현재 구현에 없는 표면과 runtime/store 작업을 `90-implementation-gap`에 기록 | **완료** — 언어별 구현 상태 기록 |
| 5. `.NET` 파일럿 | contracts, runtime, in-memory, Redis와 contract test 구현 | 구현 완료, §13.1~13.2와 package 검증 진행 |
| 6. 언어 확산 | Java/Kotlin, Node, C++에 같은 계약 구현 | 언어별 contract test 통과 |
| 7. E2E와 guide | 공통 E2E, 언어별 guide와 운영 조회 반영 | §13.3과 배포 package 검증 통과 |

정식 계약이 확정되기 전에는 한 언어에만 `UseAllocatedRoutingId`를 public API로 추가하지
않는다. application helper, 환경 변수 adapter나 test 전용 allocator로 같은 표면을 우회해서도
안 된다.

## 15. P0 확정 결정

P0 계약은 다음과 같이 확정한다.

1. 자동 할당 RID는 single-active logical socket/node identity다. routed channel에서는 전송과
   peer 구분에 사용하고, fanout에서는 packet tracking과 운영 진단에 사용한다.
2. allocation group의 member 구성과 `slotCount`는 첫 acquire 뒤 변경하지 않으며 운영 중 변경
   API를 제공하지 않는다.
3. `WaitingForSlot`은 store watch가 있으면 변경 알림으로, 없으면 기존 location polling
   interval로 재시도한다. 별도 public retry option은 제공하지 않는다.
4. lease 기본값은 heartbeat 10초, TTL 30초, fencing margin 5초와 renew timeout 3초다. 네 값은
   기존 location runtime 옵션에서 변경할 수 있지만 allocation group별 override는 제공하지 않는다.
5. 첫 acquire 직후 heartbeat와 fencing 감시를 시작하며, 그 전에는 어떤 socket도 bind하지 않는다.
6. 여러 group은 결정적인 순서로 acquire한다. P0 store interface에 batch acquire를 추가하지 않으며,
   중간 실패나 group 소진 시 이미 확보한 slot을 반환한다.
7. 특정 slot 예약과 owner affinity는 제공하지 않는다.
8. generation은 store 내부 fencing에만 사용하고 message data path에 전달하지 않는다.
9. fixed RID와 자동 할당의 혼합은 미지원 구성으로 best-effort 감지한다. allocator 참여자 밖의
   fixed RID까지 원자적으로 차단한다고 보장하지 않는다.
10. strict single-active 운영 store는 §10.2의 logical expiry, store time, linearizable acquire와
    failover durability를 모두 보장해야 한다.
11. application은 runtime service에서 `Ready` allocation을 읽을 수 있지만 slot 선택, release와
    renew는 framework가 소유한다.
12. 자동 RID를 사용하는 `SpotNode`의 entry spot은 같은 RID를 기본으로 사용하며 명시적 entry spot
    RID와 자동 RID를 함께 설정할 수 없다.

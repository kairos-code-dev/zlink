# MeshNode drain policy 계약 감사

## 1. 결론

현재 Framework의 Spot 생성은 네 언어 모두 local MeshNode에서만 수행된다. Location store를 통한 remote
Spot resolve와 send/request는 존재하지만, 다른 serving MeshNode를 선택해 `GetOrCreate`를 전달하는 공개
계약과 runtime은 없다. 따라서 `ReleaseAndRecreate`가 다음 요청에서 Spot을 자동으로 다시 구성한다는
계약은 성립하지 않는다.

`DrainNatural`도 Framework가 관찰할 수 있는 자연 종료 조건을 정의하지 않는다. 현재 구현은 user Spot이
application에서 명시적으로 닫히지 않으면 deadline까지 기다릴 수 있다. 이 동작은 요청 처리가 끝났다는
사실과 Spot lifecycle 종료를 구분하지 못한다.

MeshNode drain policy enum과 builder option을 제거하고 하나의 고정 drain 순서를 사용하는 설계 A를
채택한다. Remote Spot 자동 생성은 이 변경에 포함하지 않는다. 이후 필요하면 serving node 선택, owner
claim, activation, generation fencing과 state 복원을 포함한 별도 distributed activation draft로 검토한다.

## 2. 공개 계약 대조

[Graceful drain §2.1](../../../../framework/spec/server/54-graceful-drain-handoff.ko.md)은
`ReleaseAndRecreate`에서 다음 요청이 serving MeshNode의 `GetOrCreate`로 Spot을 다시 구성한다고 규정한다.
반면 [Spot address messaging §3·§6](../../../../framework/spec/server/24-spot-address-messaging.ko.md)은 유효한
owner row가 없으면 resolve가 not-found이고, 실패한 request를 다른 Spot이나 MeshName으로 우회하지 않는다고
규정한다.

두 계약을 함께 만족하려면 request 경로가 다음 책임을 추가로 소유해야 한다.

- serving MeshNode 선택
- remote `GetOrCreate` 전달
- 동시 생성의 단일 owner claim
- 새 owner activation과 실패 rollback
- 이전 generation fencing
- application state 복원
- 기존 SpotHandle의 stale 처리

현재 public interface와 runtime에는 이 책임을 수행하는 경로가 없다. Location row는 이미 존재하는 Spot의
owner를 resolve하기 위한 record이며 새 owner를 결정하거나 Spot을 생성하는 command가 아니다.

## 3. 언어별 구현 증거

### .NET

- `Runtime/Spots/ZLinkSpotRuntimeManager.GetOrCreateAsync`는 현재 process에 등록된 Spot factory를
  `GetNodeForSpotFactory`로 선택하고 local node에서 생성한다.
- `Runtime/Locations/ZLinkLocationAddressResolvers.ResolveSpotHandleAsync`는 기존 row를 handle로 변환하며
Spot을 만들지 않는다.
- `Runtime/Spots/ZLinkSpotNodeCatalog.TryDrainAsync`는 `ReleaseAndRecreate`일 때 local activation마다
  `CloseAsync`를 호출하고 Spot 수가 0인지 확인한다. 다른 node에 create request를 보내지 않는다.
- `DrainNatural`은 local Spot 수가 0이 될 때까지 기다리며 application의 자연 종료 조건을 별도로 관찰하지
  않는다.

### Java와 Kotlin

- `ZLinkSpotRuntime.getOrCreate`는 process-local `ZLinkSpotLifecycle.getOrCreate`를 호출한다. Kotlin도 같은
  Java runtime과 interface를 사용한다.
- `ZLinkStoreSpotHandleResolver`는 location row가 없으면 `Optional.empty()`를 반환하며 activation을
  시작하지 않는다.
- `ZLinkSpotRuntime.continueDrain`은 `RELEASE_AND_RECREATE`에서
  `ZLinkSpotLifecycle.releaseRecreatableSpots`를 호출해 local Spot만 정리한다.
- `DRAIN_NATURAL`은 local user Spot이 없어질 때까지 기다리며 Framework가 별도 업무 종료 조건을 평가하지
  않는다.

### Node.js

- `DefaultZLinkSpotManager.getOrCreate`는 `activations.getOrBegin`과 `createActivation`을 통해 지정한 local
  MeshName runtime에 Spot을 만든다.
- Location resolver는 기존 owner row를 `ZLinkSpotHandle`로 변환하며 새 Spot을 만들지 않는다.
- `DefaultZLinkSpotManager.drainForShutdown`은 `ReleaseAndRecreate`인 local activation에
  `requestDrainClose`를 호출하고 전체 local activation이 없어지기를 기다린다.
- `DrainNatural` activation은 runtime이 닫지 않으므로 application close가 없으면 deadline까지 남을 수
  있다.

### C++

- `mesh_node_builder_t::use_drain_policy`는 값을 `mesh_node_builder_state_t::drain_policy`에 저장한다.
- Runtime은 이 값을 읽어 Spot cleanup 순서를 바꾸는 분기를 제공하지 않는다. Host stop은 policy와
  관계없이 local native handle과 Spot을 닫는다.
- `spot_node_manager_t::get_or_create_spot`은 process-local manager에서 생성하고 `resolve_spot`은 등록된
  resolver의 기존 route를 읽는다.

C++ public enum과 setter는 동작 차이를 제공하지 않으므로 구현된 policy로 판단하지 않는다.

## 4. 설계 비교

| 기준 | 설계 A: policy 제거 | 설계 B: policy 유지와 distributed activation 추가 |
|---|---|---|
| Public interface | enum과 builder option을 제거하고 drain 하나만 제공 | serving 선택·remote create·claim·activation 결과와 복원 정책을 새로 제공 |
| 책임 경계 | MeshNode는 local admission, accepted work와 local cleanup만 소유 | MeshNode shutdown이 cluster placement와 domain state 복원까지 소유 |
| 기존 resolve 계약 | row가 있으면 route, 없으면 not-found를 유지 | row 부재를 create trigger로 재정의해야 함 |
| 네 언어 parity | 기존 lifecycle primitive로 구현 가능 | 네 runtime과 store에 새로운 분산 protocol이 필요 |
| 실패 의미 | 기존 deadline과 `ForceStopped`로 유한 완료 | claim 충돌, activation 실패, state 복원 실패와 stale handle 결과를 새로 정의해야 함 |

설계 A는 public interface를 줄이고 local lifecycle 책임 안에서 구현할 수 있다. 설계 B는 별도의 deep module이
필요한 기능이며 drain option에 포함하면 placement와 복원 지식이 shutdown 경로로 누출된다.

## 5. 고정 drain 목표 계약

MeshNode drain은 다음 순서를 하나의 shared operation으로 수행한다.

1. Node·Spot·Actor 신규 admission과 새 transfer 배정을 막는다.
2. Drain 전에 수락한 application turn과 request completion을 deadline까지 처리한다.
3. Actor handoff와 transfer barrier를 terminal 상태로 만든다.
4. STREAM binding과 session barrier를 terminal 상태로 만든다.
5. Application이 이미 명시적으로 닫은 Spot을 포함해 남은 local Spot을 lifecycle 순서에 맞춰 닫는다.
6. Spot·Actor location ownership, MeshNode descriptor·owner lease와 peer resource를 정리한다.
7. Deadline이나 필수 cleanup 실패가 발생하면 bounded force stop을 수행하고 terminal result를 한 번만
   완료한다.

평상시 request가 완료됐다는 사실만으로 Spot을 닫지 않는다. Drain 중 local Spot cleanup은 Framework가
시작하지만, Spot을 다른 node에 자동으로 만들거나 state를 복사·replay하지 않는다. Location row 제거 뒤
기존 SpotHandle의 호출은 stale 또는 target-not-found로 끝난다. 다른 serving node에서 Spot이 필요하면
application이 그 node의 public local `GetOrCreate`를 명시적으로 호출해야 한다.

## 6. 수정과 검증 범위

- 정식 owner: 공통 `05-framework-api`, server `20`, `24`, `40`, `54`, `90-implementation-gap`
- Exact interface: .NET, C++, Java, Kotlin, Node의 MeshNode drain enum과 builder option
- E2E owner: Config 11 `OBS-C3`를 고정 cleanup과 hidden remote create 금지 scenario로 교체
- 기존 재사용: `OBS-C1` admission, `OBS-C2` Actor handoff, `OBS-C4` STREAM·force stop,
  `OBS-C5` zero-target drain
- Sample: Bingo의 natural policy 선언과 ShoppingMall의 release policy 선언·권고 제거
- Verifier: 제거 enum·builder spellings no-hit, fixed order, `OBS-C3`의 normal request 유지·row release·
  hidden remote create 금지·명시적 local create만 허용하는 의미를 검사

기존 언어 E2E가 사용하는 `zlink.drain.rooms.drained{policy=...}` label은 정식 metric catalog의 계약이
아니다. Policy 제거와 함께 label과 `drain_natural`·`release_and_recreate` 값을 완료 증거에서 제거하고,
public lifecycle event, Spot count와 location row로 cleanup을 검증한다.

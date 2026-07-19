# Runtime Monitoring — 공통 스펙

[스펙 목차](../README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Runtime metrics](51-runtime-metrics.ko.md) · [Graceful drain](54-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0에서 MeshNode의 상태를 snapshot과 typed event로 관찰하는 공통 공개
계약을 정의한다. 이 문서는 “운영자가 peer·channel readiness, multicast backpressure·drop,
application·infrastructure claim과 drain 진행을 어떤 안정된 표면으로 확인하는가?”라는 질문에 답한다.

집계 계기 이름은 [51 Runtime metrics](51-runtime-metrics.ko.md), 메시지 한 건의 trace는
[52 Message flow tracing](52-message-flow-tracing.ko.md), drain state machine은
[54 Graceful drain](54-graceful-drain-handoff.ko.md)이 소유한다. 이 문서는 socket 내부 frame, poller와
queue 자료 구조를 공개 계약으로 정하지 않는다.

## 2. Snapshot

Runtime monitoring service는 등록된 MeshName별로 하나의 일관된 MeshNode snapshot을 반환한다. peer,
channel, multicast와 claim 상태를 서로 다른 service에서 조합하도록 호출자에게 요구하지 않는다.

| 영역 | 공개 관찰 값 |
|---|---|
| MeshNode | MeshName, RID, lifecycle generation, descriptor revision, endpoint, lifecycle state, drain state, descriptor source set |
| Peer | RID, lifecycle generation, descriptor revision, endpoint, admission state, ready, drain state, ChannelName set, last failure |
| Channel | ChannelName, local weight, ready member 수, 선택 가능 여부 |
| Logical Multicast | submit·backpressure·drop 누계, remote·local snapshot/admitted/dropped 수 |
| Claim | application·infrastructure domain별 active 여부와 pending work 수 |
| Location | store configured 여부, ready·degraded state, 마지막 성공·실패 시각 |
| Drain | state, deadline, sealed work, pending request·transfer·STREAM barrier 수 |

RID와 endpoint는 진단 snapshot에 포함할 수 있지만 metric label로 사용하지 않는다. snapshot은 호출이
끝난 뒤에도 안전한 immutable value이며 native handle이나 caller buffer를 보유하지 않는다.

snapshot에는 monotonic `Sequence`와 관찰 시각을 포함한다. 같은 MeshNode에서 더 큰 sequence가 더 나중의
상태를 뜻한다. 여러 MeshNode의 sequence를 전역 시계처럼 비교하지 않는다.

## 3. Event identifiers

공통 event identifier는 아래 문자열로 고정한다. 언어별 enum이나 record 이름은 달라도 identifier 값은
바꾸지 않는다.

| Identifier | 발생 조건 |
|---|---|
| `zlink.runtime.mesh_node.state_changed` | MeshNode lifecycle 또는 ready state 변경 |
| `zlink.runtime.mesh_node.peer_changed` | peer admission, ready, generation 또는 drain state 변경 |
| `zlink.runtime.mesh_node.channel_changed` | channel weight, ready member 수 또는 선택 가능 상태 변경 |
| `zlink.runtime.mesh_node.multicast_backpressured` | Logical Multicast admission이 backpressure를 반환 |
| `zlink.runtime.mesh_node.multicast_dropped` | local 또는 remote target별 drop 발생 |
| `zlink.runtime.mesh_node.claim_changed` | application 또는 infrastructure claim 상태 변경 |
| `zlink.runtime.mesh_node.drain_changed` | drain state 또는 sealed-work snapshot 변경 |
| `zlink.runtime.location.store_changed` | Redis location store의 ready·degraded state 변경 |

모든 event는 identifier, sequence, timestamp, MeshName과 source RID를 가진다. 해당 event에 필요한 경우에만
peer RID, lifecycle generation, descriptor revision, ChannelName, claim domain, message kind, remote·local
snapshot/admitted/dropped count, reason과 drain state를 추가한다. payload와 application metadata를 event에
복사하지 않는다.

### 3.1 닫힌 상태 값

| 필드 | 값 |
|---|---|
| MeshNode state | `starting`, `serving`, `draining`, `drained`, `force_stopping`, `stopped`, `faulted` |
| Peer state | `configured`, `connecting`, `admitted`, `ready`, `draining`, `disconnected`, `rejected` |
| Claim domain | `application`, `infrastructure` |
| Descriptor source | `manual`, `redis`, `manual_and_redis` |
| Store state | `not_configured`, `ready`, `degraded`, `stopped` |
| Multicast reason | `backpressure`, `send_timeout`, `target_closed`, `shutdown` |

정확한 오류 객체와 언어별 casing은 언어별 공개 인터페이스 문서가 정한다.

## 4. Event ordering과 coalescing

같은 MeshNode source의 event는 sequence 순서로 관찰된다. event handler가 느려도 MeshNode message dispatch와
claim progress를 막지 않는다. bounded observer queue가 가득 차면 상태 변경 event를 coalesce할 수 있지만
다음 규칙을 지켜야 한다.

- 가장 최신 snapshot sequence를 잃지 않는다.
- backpressure와 drop 누계의 증가분을 합쳐도 count를 잃지 않는다.
- terminal drain event를 drop하지 않는다.
- coalescing 또는 overflow 자체를 metric으로 기록한다.

event는 변화 알림이며 현재 상태의 authority는 snapshot이다. handler가 event sequence gap을 발견하면
최신 snapshot을 다시 읽어 상태를 맞춘다.

## 5. Observer 격리

Runtime event observer는 MeshName별 비동기 event stream을 여러 개 열 수 있다. 한 observer가 읽기를
중단하거나 느려도 다른 observer, MeshNode receive와 application callback 결과를 바꾸지 않는다. 각 stream은
호출 시 양수 capacity를 받고 독립 bounded queue를 사용한다.

Observer가 event를 받은 뒤 snapshot을 읽거나 drain, send 또는 application operation을 호출해도 monitoring
lock을 재진입하게 하지 않는다. Observer 소비 코드의 예외는 application이 소유하며 runtime dispatch 결과를
바꾸지 않는다.

## 6. Startup validation

- 등록하지 않은 MeshName의 snapshot 또는 event stream을 요청하면 구성 오류다.
- observer queue capacity가 0 이하이면 호출 인자 오류다.
- Redis location store가 없는 runtime은 location event를 만들지 않고 snapshot의 store state를 `not_configured`로 반환한다.
- metric·trace 활성화 여부와 runtime snapshot 사용 가능 여부를 묶지 않는다.

## 7. 검증 요구

- snapshot 하나로 MeshNode, peer, channel, multicast, claim과 drain state를 함께 읽을 수 있다.
- peer lifecycle generation, descriptor revision과 실제 ready state를 별도 필드로 관찰할 수 있다.
- publish operation의 backpressure 결과와 target별 drop 수가 각각 관찰된다. 같은 operation에서 둘 다 발생할 수 있다.
- application callback이 대기 중이어도 infrastructure claim change와 request completion이 관찰된다.
- observer failure나 느린 소비가 dispatch, reply와 drain terminal result를 바꾸지 않는다.
- sequence gap 뒤 snapshot 재조회로 최신 상태를 복원할 수 있다.
- snapshot의 RID, endpoint, topic, Actor ID와 Spot RID가 metric label로 복사되지 않는다.

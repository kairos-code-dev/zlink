# Service monitoring runtime

[내부 구조 목차](README.ko.md) · [Transport liveness](transport-liveness-runtime.ko.md) ·
[Concurrency and resources](concurrency-resource-runtime.ko.md)

이 문서는 RouteMesh 11.0 목표 구조를 설명한다. 현재 구현과의 차이와 완료 상태는
`framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md`가 소유한다.

## 1. Monitor layering

Raw socket monitor는 connect, disconnect, accept, bind, protocol과 transport failure를 raw connection identity와
함께 제공한다. Framework state reducer는 이 event를 peer, topology, mailbox, object, transfer와 host lifecycle에
적용한 뒤 typed event와 immutable snapshot을 만든다. Application observer는 raw monitor queue를 직접 읽지 않는다.

Monitor ingest는 selected connection identity와 protocol failure를 유실하지 않는다. Binding monitor overflow를
관측하면 raw connection snapshot을 다시 읽거나 해당 topology를 not-ready로 바꾼다. 상태를 추측하지 않는다.
Send-ready처럼 빈도가 높은 edge는 current readiness를 reducer에 적용한 뒤 counter로 합칠 수 있다. Application
observer queue의 drop 정책을 reducer ingest에 적용하지 않는다.

## 2. Event queue와 coalescing

Observer마다 독립 bounded queue를 사용한다. Queue가 가득 차면 non-terminal state event를 coalesce할 수 있지만
source별 최대 snapshot sequence, backpressure·drop counter 증가분, transfer·termination terminal event와 overflow
counter를 보존한다. Event는 변화 알림이고 snapshot이 current state authority다. Sequence gap을 관측한 consumer는
latest snapshot을 다시 읽는다. Event callback은 reducer lock 밖에서 실행한다.

## 3. Snapshot와 metric

Reducer는 MeshNode, ClientServer Channel, automatic fanout Channel과 host termination을 별도 immutable snapshot으로
만든다. Snapshot sequence는 같은 source 안에서만 비교한다. Peer snapshot은 lifecycle generation, descriptor
revision, selected connection readiness와 service state를 분리한다.

Metric은 queue depth, pending byte, active turn, request terminal reason, reconnect, multicast partial result, object
creation, transfer phase와 termination duration을 bounded-cardinality label로 집계한다. Endpoint, NodeRid, ActorId,
SpotRid, transfer ID, correlation ID와 payload를 label에 넣지 않는다. Trace correlation은 operation ID와 public flow
correlation을 runtime 내부에서 연결하며 payload와 application metadata를 event에 복사하지 않는다.

## 4. Observer lifetime

Observer close 또는 cancellation은 해당 registration만 종료한다. Cancellation을 인식한 뒤 새 event를 queue에
넣지 않고 남은 event는 폐기한다. 이미 시작한 callback은 반환할 수 있지만 close 반환 뒤 새 callback을 시작하지
않는다. Observer failure는 runtime state와 terminal result를 바꾸지 않는다.

Host 종료는 일반 producer admission을 먼저 닫되 observer마다 예약한 terminal lane을 유지한다. Accepted
completion과 resource cleanup 뒤 final snapshot과 terminal event를 한 번 게시하고 queue를 닫는다. Terminal event
뒤에는 어떤 producer도 새 event를 만들지 않는다.

## 5. 검증 기준

- Raw event가 current connection identity와 generation을 통과한 뒤에만 Framework state를 바꾼다.
- Observer overflow가 terminal event와 counter delta를 유실하지 않는다.
- Snapshot이 immutable하고 sequence gap 뒤 current state를 복원한다.
- High-cardinality identity와 payload가 metric label과 event에 포함되지 않는다.
- Observer failure와 close가 dispatch와 host terminal result를 바꾸지 않는다.

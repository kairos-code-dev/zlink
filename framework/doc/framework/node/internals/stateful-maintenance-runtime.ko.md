# Node.js stateful maintenance runtime

[Node.js 문서](../README.ko.md) · [Runtime lifecycle](runtime-lifecycle.ko.md) ·
[Location Runtime 계약](../../spec/server/40-location-runtime.ko.md) ·
[Host 종료 계약](../../spec/server/54-graceful-drain-handoff.ko.md)

## 1. 목적

이 문서는 Node.js service runtime이 Actor와 Instance Spot owner 변경을 Location Store의 opaque CAS와
Checkpoint Store 위에서 구현하는 내부 구조를 설명한다. Application adapter는 typed state만 다루며
authority key, Store version, checkpoint reference와 wire frame을 알지 않는다.

## 2. 내부 모듈

```text
+----------------------------------------------------------+
| Host maintenance barrier and target reservation          |
+----------------------------------------------------------+
| Object coordinator: phase validation and recovery        |
+----------------------------------------------------------+
| Authority codec       | Checkpoint envelope codec        |
+----------------------------------------------------------+
| Location Store port   | Checkpoint Store port            |
+----------------------------------------------------------+
```

Host barrier는 inventory와 신규 application admission의 순서를 정한다. Object coordinator는 Actor와
Instance Spot에 공통인 transaction loop를 제공하되, membership·activation callback은 object별 adapter로
분리한다. Store port는 payload를 해석하지 않고 opaque key와 expected Store version만 비교한다.

## 3. Authority CAS loop

Coordinator의 phase 변경은 항상 다음 순서로 수행한다.

1. Authority key를 읽어 payload와 Store clock snapshot을 얻는다.
2. Payload checksum, object identity, transaction generation과 허용 phase를 검증한다.
3. Store의 `leaseExpiresAt - storeNow`를 operation 시작 전 monotonic 시각에 더해 local deadline 상한을 만든다.
4. 새 payload를 immutable bytes로 encode한다.
5. 읽은 opaque Store version을 expected 값으로 compare-exchange한다.
6. Conflict면 반환된 current snapshot으로 2단계부터 다시 판정한다. 무조건 write하거나 이전 mutation을
   그대로 반복하지 않는다.

Missing 결과도 provider-issued Store version을 가지므로 최초 owner claim과 delete 뒤 재생성에 같은 loop를
사용한다. Coordinator lease 또는 local admission deadline이 끝나면 message, timer, reply completion과 phase
write를 모두 stale 결과로 끝낸다.

## 4. Phase driver

`Preparing`은 source admission seal과 accepted queue boundary를 함께 기록한다. `Captured`는 application state와
accepted journal을 immutable checkpoint envelope으로 만든 상태다. `Prepared`는 target reservation과
checkpoint reference를 authority에 고정한다. 이 세 phase에서는 current coordinator가 실패를 복구하거나
`Aborted`로 끝낼 수 있다.

`Committed`에서 owner fence가 target으로 바뀐다. 이후 source owner로 rollback하지 않는다. Target은
`Activating`에서 factory와 restore를 수행하고, accepted journal을 operation ID와 completion table로
deduplicate하며 replay한다. Ready route를 게시한 뒤 `Activated`, source cleanup 뒤 `Cleaning`, 모든 participant
정리 뒤 `Completed`로 전환한다.

각 phase callback은 CAS 성공 뒤 한 번 실행해야 하는 동작과 CAS 전에 재실행해도 안전한 동작을 분리한다.
Process가 중단된 뒤 recovery coordinator가 같은 phase를 다시 읽어도 application handler가 중복 실행되지
않도록 accepted journal cursor와 request completion을 authority payload에 포함한다.

## 5. Checkpoint envelope

Checkpoint payload는 다음 section을 versioned binary envelope 하나에 기록한다.

- object kind, logical identity와 transaction generation
- state contract ID, serializer ID와 application version
- typed application state section과 checksum
- accepted queue boundary, journal entries와 durable replay cursor
- request operation ID와 이미 확정된 completion

`Disabled`는 maintenance transfer를 시작하지 않는다. `Recreate`는 application state section을 비워 두지만
accepted journal envelope은 기록한다. `Snapshot`만 typed adapter의 capture 결과를 state section에 넣는다.
Framework는 Checkpoint Store에 24시간 retention을 전달하며 public option으로 노출하지 않는다.

Authority CAS가 checkpoint 기록 뒤 실패하면 reference를 새 authority에 재사용하지 않는다. 즉시 idempotent
delete를 시도하고 실패하면 Store retention이 orphan을 정리한다. `Get`의 missing은 먼저 authority를 다시
읽어 stale reference와 current checkpoint loss를 구분한다.

## 6. Target selection과 replacement

Target 후보는 `Serving` descriptor 중 source보다 낮지 않은 application version, factory type, state reader,
maintenance wave와 capacity 조건을 모두 만족해야 한다. Descriptor capacity 읽기는 reservation이 아니다.
Target runtime의 infrastructure mailbox가 reservation token을 발급하고, `Prepared` CAS 전에 node generation과
lease를 다시 확인한다.

Commit 전 target이 유효하지 않게 되면 reservation을 해제하고 새 target으로 `Prepared` payload를 CAS한다.
Commit 뒤 target lease가 끝나면 recovery coordinator가 같은 logical target의 새 lifecycle을 사용할 수 있는지
확인하고, 불가능하면 compatible target을 새 transaction generation으로 선택한다. 이전 target의 늦은 activation
completion은 generation fence로 거부한다.

## 7. STREAM barrier

물리 STREAM connection은 이동하지 않는다. Actor owner commit 전에는 source binding generation으로 relay하고,
commit 뒤에는 target owner와 새 binding generation만 accept한다. Pending request completion과 reply route가
terminal 상태가 될 때까지 infrastructure mailbox가 barrier를 유지한다. Runtime timer handle도 이동하지 않으며
Snapshot state에 업무 timer 정보를 기록한 경우 target이 새 timer를 등록한다.

## 8. 검증 지점

- 동일 expected Store version으로 coordinator 둘이 성공하지 않는다.
- Store clock에서 변환한 local deadline 뒤 stale owner admission이 닫힌다.
- Checkpoint reference를 authority commit 전에 target이 사용하지 않는다.
- Committed 뒤 source rollback이 발생하지 않는다.
- Replay cursor와 operation completion이 process 재시작 뒤에도 중복 handler 실행을 막는다.
- Target replacement가 이전 reservation과 activation completion을 generation으로 fence한다.
- Checkpoint delete가 missing이어도 cleanup은 성공으로 끝난다.
- User Spot은 state 추론 없이 Retire preflight를 `TransferDisabled`로 차단한다.

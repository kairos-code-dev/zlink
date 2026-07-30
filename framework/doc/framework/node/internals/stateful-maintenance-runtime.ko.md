# Node.js stateful maintenance runtime

[Node.js 문서](../README.ko.md) · [Runtime lifecycle](runtime-lifecycle.ko.md) ·
[Location Runtime 계약](../../common/spec/21-location-runtime.ko.md) ·
[Host 종료 계약](../../common/spec/28-graceful-drain-handoff.ko.md)

## 1. 목적

이 문서는 Node.js service runtime이 Entry·`PerActor` Actor, `SpotWide` User Spot aggregate와 Instance Spot
owner 변경을 Location Store의
opaque CAS와 Relocation Store 위에서 구현하는 내부 구조를 설명한다. Application adapter는 opaque bytes만
capture·restore하며 authority key, Store version, relocation reference와 wire frame을 알지 않는다.

## 2. 내부 모듈

```text
+----------------------------------------------------------+
| Host maintenance barrier and target reservation          |
+----------------------------------------------------------+
| Object coordinator: phase validation and recovery        |
+----------------------------------------------------------+
| Authority codec       | Relocation envelope codec        |
+----------------------------------------------------------+
| Location Store port   | Relocation Store port            |
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

Host `Retire`는 Entry·`PerActor` Actor, Instance Spot과 `SpotWide` User Spot aggregate queue에
infrastructure notification을 예약한다. `PerActor` User Spot은 target private shell과 Spot authority를
먼저 전환한 뒤 Actor를 각각 예약한다. Notification이 turn boundary에 도달하면 현재 실행 중인 turn만
source에서 완료한다. Process
outbound·target inbound unit, 필요한 `Capture`·`Restore` callback과 deterministic encoded upper-bound byte
permit을 모두 얻은 unit만 source admission을 reversible하게 seal한다. Permit을 얻지 못하면 notification을
다시 예약하고 application message와 timer를 계속 처리한다.

`application_signaled` SpotWide unit은 target과 permit을 먼저 준비한 뒤
`relocationReady().defer()`의 queue barrier를 기다린다. 준비된 relocation이 없으면
source에서 `continued`, precommit abort도 source에서 `continued`, commit과 target
restore가 끝나면 target에서 `relocated` callback marker를 실행한다. Callback marker가
끝나기 전에는 frozen queue, relay와 새 direct queue를 application handler에 열지 않는다.

`Preparing`은 permit을 가진 source seal과 accepted queue boundary를 함께 기록한다. `Captured`는 optional
application state, 실행하지 않은 message, accepted journal과 logical timer registration·pending tick을 immutable
relocation root로 저장하고 Location authority에 reference를 연결한 상태다. `Prepared` 전에 target reservation,
factory·restore와 journal validation·staging을 끝낸다. 이 세 phase에서는 current coordinator가 failure를
recovery하거나 `Aborted`로 끝낼 수 있다.

`Committed`에서 owner와 membership fence가 target으로 바뀐다. 이후 source owner로 rollback하지 않는다.
Infrastructure relocation은 application의 join·leave callback을 실행하지 않는다. Entry·`PerActor` Actor는
accepted journal·queue·Actor timer와 source relay를 복원한다. 일반 application join만 membership callback을
사용한다. `SpotWide` User Spot aggregate도 membership callback을 호출하지 않는다.

`Activated`는 callback과 replay가 끝났지만 admission은 sealed인 상태다. `Cleaning`에서 남은 source scope와
participant state를 정리한다. `Completed` CAS, bound STREAM route ACK와 steady normalization을 모두 끝낸 뒤에만
Ready route와 application admission을 공개한다.

각 phase callback은 CAS 성공 뒤 한 번 실행해야 하는 동작과 CAS 전에 재실행해도 안전한 동작을 분리한다.
Process가 중단된 뒤 recovery coordinator가 같은 phase를 다시 읽어도 application handler가 중복 실행되지
않도록 accepted journal cursor와 request completion을 authority payload에 포함한다.

## 5. Relocation envelope

Relocation payload는 다음 section을 deterministic binary stream으로 기록한다.

- object kind, logical identity, object·owner generation과 aggregate participant manifest
- Snapshot adapter가 반환한 opaque application bytes와 checksum
- 실행하지 않은 message queue, accepted journal과 durable replay cursor
- logical timer registration, pending tick과 frozen ordering boundary
- request operation ID, reply route와 이미 확정된 completion

Application이 byte format, version, compatibility와 migration을 소유한다. Framework는 state contract ID,
serializer ID, generic state type과 relocation codec을 제공하거나 descriptor·metric에 싣지 않는다.
`DisableRelocation`은 cross-node relocation을 capture 전에 거부한다. `RecreateOnRelocation`은 application state section만 비워 두고
queue·journal·timer를 포함한 complete envelope을 기록한다. `PreserveStateWith`만 adapter의 `Capture` 결과를 application
state section에 넣는다. Participant별 captured application state는 최대 64 MiB다.

Framework는 participant별 capture 상한과 Framework-owned section의 deterministic upper bound로 byte permit을
seal 전에 all-or-nothing으로 확보하고, `Capture` 뒤 actual encoded size로 reservation을 줄인다. 한 aggregate의
upper bound가 payload window를 넘으면 window가 빈 상태에서 exclusive unit으로만 진행한다.

Authority CAS가 relocation root 기록 뒤 실패하면 reference를 새 authority에 재사용하지 않는다. 즉시
idempotent delete를 시도하고 실패하면 Store retention이 orphan을 정리한다. `Get`의 missing은 먼저 authority를
다시 읽어 stale reference와 current relocation data loss를 구분한다.

## 6. Target selection과 replacement

Target 후보는 `Serving` descriptor 중 source보다 낮지 않은 application version, factory type, Snapshot adapter
capability, maintenance wave와 capacity 조건을 모두 만족해야 한다. Descriptor capacity 읽기는 reservation이 아니다.
Target runtime의 infrastructure mailbox가 reservation token을 발급하고, `Prepared` CAS 전에 node generation과
lease를 다시 확인한다.

Commit 전 target이 유효하지 않게 되면 reservation을 해제하고 새 target으로 `Prepared` payload를 CAS한다.
Commit 뒤 target lease가 끝나면 recovery coordinator가 같은 logical target의 새 lifecycle을 사용할 수 있는지
확인하고, 불가능하면 compatible target을 새 transaction generation으로 선택한다. 이전 target의 늦은 activation
completion은 generation fence로 거부한다.

## 7. STREAM barrier

물리 STREAM connection은 이동하지 않는다. Actor owner commit 전에는 source binding generation으로 relay하고,
commit 뒤에는 target owner와 새 binding generation만 accept한다. Pending request completion과 reply route가
terminal 상태가 될 때까지 infrastructure mailbox가 barrier를 유지한다. Native timer handle과 callback
continuation은 이동하지 않는다. Framework가 relocation envelope의 logical timer registration으로 target native
timer를 만들고 pending tick을 frozen queue ordering boundary에 맞춰 복원한다. Application `Restore`가 timer를
다시 등록하지 않는다.

## 8. 검증 지점

- 동일 expected Store version으로 coordinator 둘이 성공하지 않는다.
- Store clock에서 변환한 local deadline 뒤 stale owner admission이 닫힌다.
- Relocation reference를 Location authority가 publish하기 전에 target이 사용하지 않는다.
- Committed 뒤 source rollback이 발생하지 않는다.
- Replay cursor와 operation completion이 process 재시작 뒤에도 중복 handler 실행을 막는다.
- Target replacement가 이전 reservation과 activation completion을 generation으로 fence한다.
- Relocation delete가 missing이어도 cleanup은 성공으로 끝난다.
- Current turn만 source에서 끝나고 frozen queue·journal·timer는 target에서 순서를 보존해 복원된다.
- `SpotWide` User Spot과 member Actor가 하나의 aggregate permit·root·commit generation을 사용한다.
- Entry·`PerActor` Actor maintenance는 application membership callback 없이 queue·timer·relay를 복원한다.
- `PerActor` Spot authority 전환 뒤 `ToSpot`과 Actor별 `ToActor` route가 분리되고 stale source route가
  operation identity와 reply correlation을 유지해 relay된다.

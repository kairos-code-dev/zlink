# Transport Liveness — 공통 스펙

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[Location runtime](40-location-runtime.ko.md) · [Runtime monitoring](50-runtime-monitoring.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0 service runtime이 RouteMesh, ClientServer와 manual·automatic classic fanout 연결의
생존 상태를 판정하고 reconnect할 때 사용자가 관찰하는 결과를 정의한다. Service liveness command, raw transport
monitor와 timer 배선은 application public API가 아니며 언어별 internals가 소유한다.

Location owner lease, STREAM session heartbeat와 request timeout은 서로 다른 목적의 계약이다. 어느 하나를
다른 하나의 대체 신호로 사용하지 않는다.

## 2. Public API 경계

Framework builder는 service liveness interval과 peer deadline을 공개하지 않는다. 각 언어 service runtime은
binding의 public raw socket API와 Framework service protocol을 사용해 같은 profile을 적용한다. Private binding
member, native symbol 직접 호출과 언어별 숨은 사용자 option은 허용하지 않는다.

Framework의 probe interval은 5초이고 peer deadline은 15초다. 이 값은 service runtime 전체에
적용하며 Channel, handler 또는 peer마다 다르게 설정하지 않는다. RouteMesh·ClientServer의 양방향 연결과
classic fanout의 단방향 연결은 같은 시간 기준을 사용하지만, 서로 다른 wire 규칙을 적용한다.

### 2.1 RouteMesh와 ClientServer

Service admission이 성공하면 connection은 최초 ready 상태가 되고 15초 peer deadline이 시작된다. Application
traffic과 무관하게 5초마다 probe tick을 실행한다. Connection마다 outstanding probe ID는 최대 하나다. Outstanding
ID가 없으면 새 non-zero connection-local ID를 할당해 `livenessProbe`로 보내고, 있으면 같은 ID를 재전송한다.
Peer는 같은 connection에서 받은 ID를 `livenessAck`으로 그대로 반환한다. Current connection의 current outstanding
ID와 일치하는 첫 ACK만 deadline을 15초로 갱신하고 outstanding ID를 제거한다. Duplicate ACK, 이전 ID의 ACK와
다른 physical connection의 ACK는 무시한다. Application frame을 포함한 다른 inbound traffic은 진단 시각만
갱신하며 peer deadline을 연장하지 않는다. Deadline을 넘으면 connection을 not-ready로 바꾸고 닫는다. Probe와
ACK는 payload와 metadata를 포함하지 않고 application queue나 handler에 전달되지 않는다.

### 2.2 Classic fanout

PUB는 송신 전용이고 SUB는 수신 전용이므로 subscriber가 같은 physical connection으로 ACK를 보낼 수 없다.
Framework는 classic fanout에 `livenessProbe`와 `livenessAck`을 사용하지 않는다. Subscriber는 automatic
descriptor의 publisher마다, manual mode에서는 endpoint마다 전용 SUB socket과 receive loop를 하나씩 둔다.
여러 publisher를 한 SUB socket에 함께 연결하지 않는다. 이 격리는 수신 frame과 timeout을 해당 publisher
connection에 정확히 연결하고 한 publisher의 장애가 다른 publisher를 not-ready로 바꾸지 않게 한다.

Publisher는 application event 송신 여부와 무관하게 5초마다 같은 PUB endpoint로 단방향 beacon을 보낸다.
Beacon은 topic frame `01 5A 4C 46 31`과 payload frame `5A 46 01 01`로 이루어진 정확히 두 frame의
multipart record다. Public
fanout publish에서 이 exact topic은 사용할 수 없으며 호출 인자 오류로 실패한다. Subscriber는 전용 socket에서
유효한 application fanout record 또는 exact beacon을 처음 받은 뒤 해당 publisher를 ready로 바꾸고,
이후 수신마다 last-receive 시각을 갱신한다. 15초 동안 둘 다 받지 못하면 그 publisher만 not-ready로 바꾸고
socket을 닫은 뒤 current intent에 따라 다시 연결한다. Beacon은 acknowledgement를 요구하지 않으며 application
queue, handler, message-flow publish와 fanout 수신 metric에 전달하지 않는다.

Reserved topic 예약은 topic byte 전체가 exact 값과 일치할 때만 적용하며 같은 prefix의 다른 topic은 public
application topic으로 사용할 수 있다. Reserved topic인데 payload가 다르거나 frame 수가 2가 아니면 protocol
error다. Subscriber는 이 record를 application에 전달하거나 receive activity로 인정하지 않고 해당 publisher를
즉시 not-ready로 바꾼 뒤 전용 socket을 닫는다.

Orderly close와 transport disconnect event는 service liveness deadline을 기다리지 않고 즉시 ready index에
반영한다. 이전 physical connection에서 늦게 도착한 ACK는 current connection의 deadline을 갱신하지 않는다.

## 3. Ready와 장애 판정

RouteMesh와 ClientServer connection은 transport 연결, service admission, identity·generation 검증과 handler
readiness가 모두 성공한 뒤에만 ready다. Classic fanout publisher connection은 전용 SUB socket의 transport
연결과 descriptor 또는 manual endpoint association이 유효하고, 그 socket에서 첫 valid application record나
beacon을 받은 뒤에만 ready다. Descriptor가 존재하거나 connect operation이 수락됐다는 사실만으로 어느
topology도 ready가 되지 않는다.

다음 조건 가운데 하나를 관찰하면 해당 connection을 ready index에서 제거한다.

- orderly disconnect 또는 transport 오류
- RouteMesh·ClientServer service liveness peer timeout 또는 fanout publisher inbound timeout
- identity, lifecycle generation 또는 security admission 실패
- 더 큰 lifecycle generation의 같은 peer가 admission됨
- host가 `Draining`, `Stopped` 또는 `Error`로 바뀌어 새 selection을 허용하지 않음

Peer 하나의 실패는 host 전체를 `Error`로 바꾸지 않는다. 다른 ready peer와 local owner는 계속 처리할 수
있다. Ready peer가 0개가 되면 Channel selection은 target-not-found 또는 route-not-connected 계약에 따라
완료하며 timeout을 늘려 실패를 숨기지 않는다.

## 4. In-flight operation과 reconnect

Connection loss와 request reply가 경쟁하면 correlation owner가 terminal 결과 하나만 완료한다. Transport가
request를 수락하기 전에 connection을 잃으면 route-not-connected로 끝낸다. 수락 여부를 확정할 수 없거나
이미 수락된 request는 다른 peer에 자동 재제출하지 않으며 reply, request timeout, cancellation, shutdown
또는 route failure 가운데 하나로 완료한다.

Reconnect는 같은 configured intent 또는 current discovery descriptor를 사용한다. RouteMesh와 ClientServer의
새 connection은 admission을 다시 통과해야 하며 이전 connection ID, reply route, session binding과 ready
상태를 재사용하지 않는다. Fanout은 해당 publisher 전용 SUB socket을 새로 만들고 첫 valid receive 전에는
ready로 복원하지 않는다. 같은 RID의 더 큰 lifecycle generation은 새 peer identity이고, 이전 generation의
늦은 event와 frame은 현재 connection을 변경하지 못한다.

## 5. Location Store와의 관계

Store descriptor와 owner lease는 discovery·placement authority를 제공하지만 transport readiness를 증명하지
않는다. Store polling 장애가 발생해도 이미 연결된 peer의 transport liveness 판정은 계속 진행한다. 반대로
service liveness frame을 받아도 owner lease가 만료된 descriptor나 object owner를 placement·transfer authority로
사용하지 않는다.

Location option의 owner lease renew interval은 store lease 갱신 주기이며 service liveness interval이 아니다.
두 값을 같은 이름이나 public option으로 합치지 않는다.

## 6. Host 종료와 resource 정리

`Retire`와 `Shutdown`이 admission을 seal한 뒤에도 accepted reply, transfer와 STREAM barrier에 필요한 기존
connection은 deadline까지 유지할 수 있다. 새 application target selection에는 포함하지 않는다. Terminal
cleanup은 liveness timer, reconnect timer, monitor subscription과 pending callback을 connection보다 늦게
남기지 않는다.

## 7. Observability

Runtime snapshot은 configured intent, connecting, admitted, ready, reconnecting과 last failure를 구분한다.
Orderly disconnect와 service liveness timeout은 서로 다른 reason으로 기록한다. Metric label에는 endpoint, RID와
connection ID를 넣지 않으며 개별 identity는 bounded snapshot과 trace에서만 제공한다.

## 8. 검증 요구

- Orderly disconnect는 service liveness deadline을 기다리지 않고 ready index에서 제거된다.
- Half-open connection은 15초 liveness deadline 안에 not-ready가 된다.
- RouteMesh·ClientServer admission이 initial ready와 15초 deadline을 만들며 application traffic 없이도 5초마다
  probe tick을 실행한다.
- Connection마다 outstanding probe ID가 최대 하나이고 ACK 전 tick은 같은 ID만 재전송한다.
- Current connection의 current outstanding ID와 일치하는 첫 ACK만 deadline을 갱신하고 ID를 제거하며 duplicate,
  previous ID와 다른 connection의 ACK는 deadline을 갱신하지 않는다.
- Probe와 ACK는 application handler로 전달되지 않고 다른 inbound service frame은 deadline을 갱신하지 않는다.
- Fanout subscriber가 publisher마다 전용 SUB socket을 사용하고 같은 socket에서 받은 첫 valid application
  record 또는 exact beacon 뒤에만 ready가 된다.
- Fanout beacon은 reserved topic·payload의 두-frame 규칙을 지키며 ACK, application dispatch와 publish·receive metric을
  만들지 않는다.
- Malformed reserved-topic record는 protocol error로 해당 publisher만 즉시 not-ready로 만들며 application
  delivery와 liveness activity를 만들지 않는다.
- 한 fanout publisher가 15초 동안 수신 activity를 만들지 못하면 해당 publisher만 not-ready가 되고 다른
  publisher connection은 유지된다.
- Fanout publisher는 application publish와 무관하게 5초마다 beacon을 전송한다.
- Peer 한 개의 실패가 다른 peer와 host state를 `Error`로 바꾸지 않는다.
- Store polling 장애 중에도 transport liveness가 진행되고, transport ready가 만료 owner lease를 되살리지 않는다.
- Reconnect가 service admission을 다시 수행하고 이전 connection의 completion·binding state를 재사용하지 않는다.
- Reply, timeout, cancellation, disconnect와 shutdown 경쟁에서 terminal completion이 하나만 발생한다.
- Connection loss 뒤 request와 one-way operation을 다른 peer나 owner에 자동 재제출하지 않는다.
- `Retire`·`Shutdown` cleanup 뒤 liveness·reconnect timer와 callback이 남지 않는다.
- C++·.NET·JVM·Node.js가 같은 기본 profile과 관찰 결과를 제공한다.

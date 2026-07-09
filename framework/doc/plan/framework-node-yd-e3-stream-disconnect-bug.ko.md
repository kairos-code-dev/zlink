# Node YieldDispatch YD-E3 stream disconnect bug

이 문서는 완료 보고가 아니라 재현 기록이다. Node `YieldDispatch` `YD-E3` shutdown wait를
재검증하는 과정에서 stream request가 public closed/cancelled error로 끝나지 않는 문제가 확인되었다.

## 증상

`framework/languages/node/e2e/YieldDispatch/run_e2e.sh YD-E3`를 실행하면 runner는 `scenario YD-E4 passed`
이후 shutdown wait 단계로 들어간다. `play-a`는 pending yield 중 정상 종료되고, session role은
`yield.spot.route` disconnect를 `ZLinkRouteDisconnectedError`로 감지한다.

하지만 client의 `shutdown-wait` request는 `yield-dispatch shutdown wait result=passed` marker를 출력하지
않는다. session role은 `session-disconnected` evidence를 남긴 뒤에도 Node 프로세스가 CPU 한 코어를 계속
사용한다.

## 현재 증거

실행 중 관찰한 flow/evidence:

```text
message flow phase=error surface=streamSession kind=request label=session-a packet=YieldShutdownScenarioReq corr=1 src=00000001 errorReason=handlerException errorAction=replyError errorType=ZLinkRouteDisconnectedError errorMessage=Route channel 'yield.spot.route' disconnected: 512/4
session-disconnected|rid=session-a|session=00000001
```

stream error reply를 blocking send로 쓰면 `stream.send()`가 반환하지 않고 session role 프로세스가 CPU를
계속 사용한다. error reply를 non-blocking send로 바꾸면 handler는 session close까지 진행하지만, raw
client는 닫힘을 관찰하지 못한다.

## 작은 재현

Node binding regression에 아래 테스트를 추가했다.

```text
bindings/node/tests/stream_send_regression.test.ts
```

focused 실행:

```bash
cd bindings/node
npm run build
timeout 30s node --test dist-tools/tests/stream_send_regression.test.js
```

실패 결과:

```text
not ok 1 - stream disconnectRid from packet handler closes the raw client
Error: socket close timed out
```

이 테스트는 raw TCP client가 stream socket에 frame을 보내고, stream packet handler가 받은 routing id로
`disconnectRid(rid)`를 호출한다. 기대 동작은 raw client가 close/end/error 중 하나를 관찰하는 것이다.
현재 checkout에서는 닫힘이 전파되지 않는다.

## 의심 지점

Node binding의 `StreamSocket.disconnectRid()`는 native `socketDisconnectRid` export를 호출하고, native export는
`zlink_disconnect_rid()`를 호출한다. core 쪽 stream 구현은 `stream_t::xterm_peer_rid()`에서 routing id
크기를 검사한 뒤 `terminate_out_pipe_by_routing_id()`를 호출한다.

관련 위치:

```text
bindings/node/src/zlink/runtime/sockets/stream_socket.ts
bindings/node/native/src/addon_core.cc
core/src/api/socket/socket_api.cpp
core/src/runtime/sockets/stream/stream.cpp
```

Node에서 빈 payload를 같은 routing id로 보내는 방법도 확인했지만 raw client는 close/end/error를
관찰하지 못했다. `stream.options.notify = true`도 raw client 닫힘 전파를 만들지 못했다. 반면 stream
socket 전체 `close()`는 raw client가 `end`를 관찰하게 만든다. 따라서 문제는 stream socket 전체 종료가
아니라 peer 단위 `disconnectRid()` 의미에 있다.

Node framework에서 request timeout이나 retry로 감추면 안 되고, stream peer termination 의미를 하위
계층에서 확인해야 한다. 재현 테스트는 일반 suite를 계속 깨지 않도록 현재 `test.skip` 상태로 남겼다.
하위 계층 수정 뒤 skip을 제거하고 focused test와 전체 test를 통과시켜야 한다.

## 하위 계층 상태

core 쪽 원인은 별도로 확인되었다. STREAM packet-dispatch 경로에서 RID 라우팅은 만들어졌지만,
`zlink_disconnect_rid()`가 pipe 종료 뒤 session/engine 종료를 즉시 전파하지 못해 TCP file descriptor가
살아 있었다. 이 때문에 Node binding 재현에서 raw TCP client가 close/end/error를 관찰하지 못했다.

core 수정은 로컬에는 적용되었지만, 이 문서 작성 시점의 Node binding 배포본에는 아직 반영되지 않았다.
이미 릴리스된 `core/v8.6.2`에는 이 수정이 포함되지 않는다. Node 쪽 완료 판정은 새 패치 버전과 binding
native artifact 갱신이 끝난 뒤에만 다시 시도한다. 예를 들어 `8.6.3` 같은 새 버전으로 tag, GitHub Actions
release, bindings update가 완료된 뒤 focused binding test와 `YD-E3` runner를 재검증한다.

## 완료 조건

- `disconnectRid(rid)`가 stream packet handler에서 호출되어도 raw client가 닫힘을 관찰한다.
- Node binding native artifact가 core stream disconnect 수정이 포함된 새 패치 버전을 가리킨다.
- Node `YieldDispatch` `YD-E3` shutdown wait가 request timeout 없이 public closed/cancelled error로 끝난다.
- `session-a` role이 shutdown wait 뒤 CPU 한 코어를 계속 사용하지 않는다.
- `framework/languages/node/e2e/YieldDispatch/run_e2e.sh YD-E3`가 아래 marker를 출력한다.

```text
yield-dispatch shutdown wait result=passed
yield-dispatch shutdown recovery result=passed
scenario YD-E3 passed
```

## 범위

core는 수정하지 않는다. core 또는 native stream socket 문제가 맞다면 Node에서 retry, sleep, request timeout
완화로 통과시키지 않는다. 다른 언어에서 같은 stream disconnect 의미가 통과하는지 확인한 뒤 하위 계층
수정 항목으로 분리한다.

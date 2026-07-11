<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:end -->

# Node Stream Connector

Node Stream Connector는 `@zlink-systems/stream-connector` 패키지로 제공되는 client
connector다. 서버 framework와 별도 모듈이며 TCP, request/reply, dispatch(Manual/Immediate), typed
payload API를 client code에서 사용하게 한다. JSON, MessagePack, Protobuf, custom codec은
기존처럼 connector options의 codec registry에 등록하고, typed send/request/on/wait 표면이
그 registry로 업무 DTO를 encode/decode한다.

## Inbound Observer

`observeInbound(...)`는 수신 frame을 읽기 전용으로 관찰하는 API다. connector를 만든 뒤
`connect(...)`를 호출하기 전에 등록한다. 연결이 시작된 뒤 등록하면 오류를 던진다.

```ts
const registration = client.observeInbound((observation) => {
  console.log(
    `stream-inbound kind=${observation.kind} ` +
    `name=${observation.name} bytes=${observation.payloadLength}`);
});
await client.connect();
```

observation에는 message kind, packet name, codec, request sequence, metadata,
payload byte length, 압축 여부, 수신 시간, payload preview가 들어간다. metadata와 preview는
snapshot이므로 observer가 바꿔도 request 완료나 `on(...)` handler가 보는 값은 바뀌지
않는다. payload preview 기본 길이는 0이다.

observer callback은 receive 경로에서 직접 실행하지 않는다. callback 실패는
`ZlinkStreamErrorCode.ObserverFailed`, queue overflow는
`ZlinkStreamErrorCode.ObserverDropped`로 error handler에 보고한다. 이 오류는 관찰 기능의
진단 신호이며 원래 수신 frame 처리를 막지 않는다.

queue 기본 크기는 1024개 notification이다. 테스트나 제한된 client 환경에서는
`maxInboundObserverNotifications` option으로 조정할 수 있다.

수신된 user send message 는 observer notification 과 다른 queue 를 사용한다.
`maxReceivedMessages` 는 `on(...)`, `waitFor(...)` 같은 received-message handler 로
넘어갈 send frame queue 의 최대 크기를 정한다. 기본값은 1024개 message 다.
이 queue 가 꽉 차면 해당 send message 는 버리고
`ZlinkStreamErrorCode.ReceivedMessageDropped` 를 error handler 에 보고한다.
response, request error, heartbeat control frame 은 request 완료와 연결 상태 유지에
필요하므로 이 제한에 넣지 않는다.

## 세션 종료 사유 (close reason)

connector 의 disconnect 이벤트는 `closeReason` 을 노출한다. 값은 서버 측 `close_reason`
([runtime-metrics §4.1](../../runtime-metrics.ko.md))과 정합하는 닫힌 union 이다:
`'ClientClose' | 'IdleTimeout' | 'HeartbeatTimeout' | 'ServerDrain' | 'ProtocolError' | 'TransportError'`.
서버가 우아한 종료(graceful drain)로 세션을 닫으면 `'ServerDrain'` 이 오며, 클라이언트는 이 값을 보고
재접속·백오프를 결정한다([Graceful Drain & Handoff §7.1](../../graceful-drain-handoff.ko.md)). 대체
endpoint 지정(reconnect hint)은 별도 후속 스펙이다.

## 회귀 테스트

stream connector 문서는 connector 표면이 framework server 표면과 다른 책임을 가진다는 점을
계속 유지해야 한다. 아래 회귀 항목이 이 문서와 구현을 함께 고정한다.

- `test/contract/documentation-regression.test.js`
  - spec 문서가 회귀 테스트 절을 계속 포함하는지 확인한다.
- `test/contract/stream-connector*.test.js`
  - connector wait builder, codec decode, inbound observer가 public surface대로 동작하는지 확인한다.
- `samples/*`
  - sample client가 connector helper와 `waitFor(...)`를 기본 경로로 사용하는지 확인한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:bottom:end -->

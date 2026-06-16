<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md)
<!-- framework-adapter-nav:end -->

# Node Stream Connector

Node Stream Connector는 `@zlink-systems/stream-connector` 패키지로 제공되는 client
connector다. 서버 framework와 별도 모듈이며 TCP, request/reply, push dispatch, typed
codec helper를 client code에서 사용하게 한다.

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
[문서 목록](../../../../doc/README.ko.md)
<!-- framework-adapter-nav:bottom:end -->

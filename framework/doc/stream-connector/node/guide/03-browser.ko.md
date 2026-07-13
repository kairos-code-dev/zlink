# 03 — 브라우저

[← 목차](INDEX.ko.md) | [이전: Node](02-node.ko.md)

---

브라우저 웹 client와 웹으로 빌드한 게임 엔진에서는 browser entrypoint를 사용한다. 이 진입점은
운영체제 소켓을 사용하지 않고 플랫폼의 네이티브 `WebSocket`에 연결한다.

## 연결

```ts
import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector/browser';

const client = zlinkStreamConnectorFactory.create({
  endpoint: 'wss://game.example.com/stream', // 브라우저는 ws 또는 wss만 사용한다.
  codec: zlinkStreamJsonCodec,
  dispatchMode: ZlinkStreamDispatchMode.Immediate
});

await client.connect(); // 플랫폼 WebSocket 연결이 준비될 때까지 기다린다.
```

`tcp://`와 `tls://` endpoint는 브라우저에서 사용할 수 없으며 connector 생성 시
`ConfigurationError`로 거부된다. 연결 뒤 send, request, `waitFor`, handler 등록과 종료 방법은
Node 진입점과 같은 public 표면을 사용한다.

## 현재 비동기 flow 제한

브라우저 표준에는 Node의 `AsyncLocalStorage`와 같은 비동기 실행 문맥 기능이 없다. 그 때문에
handler가 `await`로 기다리는 동안 관련 없는 timer나 UI callback이 같은 connector에서 메시지를
보내면, 현재 구현은 inbound flow id가 그 callback에 노출되지 않는다고 보장하지 못한다.

이 제한을 감추기 위해 callback을 동기로 바꾸거나 임의의 지연을 추가하지 않는다. application도
flow id를 직접 저장하거나 전달하는 우회 코드를 만들지 않는다. 공통 MFLOW-EXT-014를 만족하는
브라우저 비동기 문맥 수단이 정식으로 결정될 때까지 이 항목은 implementation gap으로 유지한다.

transport, wire, request/reply와 수신 기능은 browser bundle 및 네이티브 `WebSocket` event 계약을
사용한 회귀 검사로 확인한다. 실제 브라우저 프로세스에서의 WSS 검증은 배포 전 환경에서도 다시
실행해야 한다.

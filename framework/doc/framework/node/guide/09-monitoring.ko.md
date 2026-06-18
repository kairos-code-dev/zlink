# Monitoring — runtime 이벤트 관찰

monitoring 은 runtime 상태 변화를 typed event 로 전달한다. socket 은 backend monitor
event 를 framework event 로 바꾸고, registry 와 Spot 은 snapshot diff 로 event 를
합성한다.

## 1. event handler

```ts
export class RegistryMonitor
  implements ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {
  async handle(event: ZLinkRegistryEvent) {
    if (event.event === ZLinkRegistryEventKind.TopologyChanged) {
      console.log(event.topology?.length ?? 0);
    }
  }
}
```

## 2. source

| source | 방식 |
|--------|------|
| socket | raw monitor event 를 framework event 로 변환 |
| registry | status, topology, service summary snapshot diff |
| Spot | status, peers, subjects snapshot diff |

discovery 는 별도 runtime event 가 아니다. 현재 provider 상태는 registry query 로 본다.

## 회귀 테스트

socket, registry, Spot monitoring source 는 `test/contract/monitoring-runtime.test.js`
에서 확인한다.

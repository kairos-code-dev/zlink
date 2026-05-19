<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework NestJS Channel Messaging](./nestjs-channel-messaging.ko.md) | [다음: Draft -- ZLink Framework NestJS Registry](./nestjs-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Registry](./nestjs-registry.ko.md)

# Draft -- ZLink Framework NestJS Monitoring

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `NestJS`에서 socket, discovery, registry, spot runtime
> event를 어떤 표면으로 올릴지 정리한다.

## 1. 방향

- event kind는 enum으로 둔다.
- 실제 callback payload는 source 이름과 상세 정보를 가진 object로 둔다.
- socket/discovery는 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.

## 2. 등록 예시

```ts
ZLinkModule.forRoot({
  monitoring: {
    socket: [{ sourceName: 'profile.server', events: SocketEvent.All }],
    discovery: [{ sourceName: 'profile.client.discovery', events: ['all'] }],
    registry: [{ sourceName: 'registry', intervalMs: 1000 }],
    spot: [{ sourceName: 'stage-node', intervalMs: 1000 }],
  },
});
```

## 3. Handler 예시

```ts
@Injectable()
export class ProfileSocketMonitor
  implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
  async handle(event: ZLinkSocketEvent): Promise<void> {
  }
}
```

source 이름은 logical name을 쓰는 편이 자연스럽다.

- socket: `profile.server`, `profile.client`
- discovery: `profile.client.discovery`
- registry: `registry`
- spot: `stage-node`

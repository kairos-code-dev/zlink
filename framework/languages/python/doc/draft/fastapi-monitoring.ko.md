<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework FastAPI Channel Messaging](./fastapi-channel-messaging.ko.md) | [다음: Draft -- ZLink Framework FastAPI Registry](./fastapi-registry.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Python 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Registry](./fastapi-registry.ko.md)

# Draft -- ZLink Framework FastAPI Monitoring

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `FastAPI`에서 socket, discovery, registry, spot runtime
> event를 어떤 표면으로 올릴지 정리한다.

## 1. 방향

- event kind는 enum으로 둔다.
- 실제 callback payload는 dataclass로 둔다.
- socket/discovery는 하부 monitor를 감싼다.
- registry/spot는 snapshot diff 기반으로 다시 올린다.

## 2. 등록 예시

```python
add_zlink_monitoring(
    app,
    socket=[{'source_name': 'profile.server', 'events': SocketEvent.ALL}],
    discovery=[{'source_name': 'profile.client.discovery', 'events': ['all']}],
    registry=[{'source_name': 'registry', 'interval': 1.0}],
    spot=[{'source_name': 'stage-node', 'interval': 1.0}],
)
```

## 3. Handler 예시

```python
class ProfileSocketMonitor(ZLinkRuntimeEventHandler[ZLinkSocketEvent]):
    async def handle(self, event: ZLinkSocketEvent) -> None:
        return None
```

source 이름은 logical name을 쓰는 편이 자연스럽다.

- socket: `profile.server`, `profile.client`
- discovery: `profile.client.discovery`
- registry: `registry`
- spot: `stage-node`

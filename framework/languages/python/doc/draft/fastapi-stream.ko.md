<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework FastAPI SPOT](./fastapi-spot.ko.md) | [다음: Draft -- ZLink Framework Python Interface Catalog](./handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Python 묶음](./README.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM open items](./stream-open-items.ko.md)

# Draft -- ZLink Framework FastAPI STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `FastAPI`에서 `STREAM`을 어떤 표면으로 올릴지 정리한다.

## 1. 방향

`STREAM`은 packet session과 raw session 두 축으로 설명한다.
recv loop를 application 표면에 직접 올리지 않는 편을 기본으로 본다.

## 2. Session

```python
class ZLinkStream(Protocol):
    async def write(
        self,
        payload: Message,
        flags: SendFlags = SendFlags.NONE,
    ) -> None: ...


class ZLinkPacketStreamSession(Protocol):
    async def on_packet(
        self,
        stream: ZLinkStream,
        header: Message,
        payload: Message,
    ) -> None: ...

class ZLinkRawStreamSession(Protocol):
    async def on_raw(
        self,
        stream: ZLinkStream,
        payload: Message,
    ) -> None: ...
```

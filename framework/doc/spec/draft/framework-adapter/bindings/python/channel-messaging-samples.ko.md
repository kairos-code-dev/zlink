[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [channel](./fastapi-channel-messaging.ko.md)

# Draft -- ZLink Framework Python Channel Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python` channel messaging 초안을 샘플로 보기 위한 문서다.

## 1. route 안에서 호출

```python
app = FastAPI()
add_zlink_framework(
    app,
    channels={"profile": ChannelOptions(client=ClientCapabilityOptions())},
    discovery=["tcp://registry1:5551"],
)

@app.post("/profiles/get")
async def get_profile(
    request: GetProfileHttpRequest,
    client: ZLinkClient = Depends(get_zlink_client),
) -> GetProfileReply:
    return await client.request(
        "profile",
        GetProfileRequest(request.account_id),
    )
```

## 2. options 예시

```python
reply = await client.request(
    "profile",
    GetProfileRequest(account_id),
    ZLinkRequestOptions(timeout=0.2, packet_name="profile.get"),
)
```

기본은 payload 타입 이름이고, `packet_name`은 override 용도다.

## 3. 수동 연결과 런타임 제어

```python
add_zlink_framework(
    app,
    channels={
        "profile": ChannelOptions(
            client=ClientCapabilityOptions(
                manual_connections=[
                    "tcp://10.0.10.15:7101",
                ],
            ),
        ),
    },
)
```

```python
class WarmupService:
    def __init__(self, connections: ZLinkChannelConnectionManager) -> None:
        self._connections = connections

    def warmup(self) -> None:
        self._connections.get_client("profile").connect(
            "tcp://10.0.10.17:7101",
        )
```

## 4. 일반 event publish

```python
await event_publisher.publish(
    "profile",
    "profile.cache-refreshed",
    ProfileCacheRefreshed(account_id),
    ZLinkSendOptions(packet_name="profile.cache-refreshed"),
)
```

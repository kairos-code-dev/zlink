[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md)

# Draft -- ZLink Framework FastAPI Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `FastAPI`에서 channel messaging을 어떤 표면으로
> 드러낼지 정리한다.

## 1. App 등록

```python
app = FastAPI()

add_zlink_framework(
    app,
    channel_name="api",
    outbound_channels=["profile", "account"],
    discovery=["tcp://registry1:5551", "tcp://registry2:5551"],
)
```

수동 연결은 아래처럼 둔다.

```python
add_zlink_framework(
    app,
    manual_connections={
        "profile": [
            {"target_rid": "01HZX...", "endpoint": "tcp://10.0.10.15:7101"},
        ]
    },
)
```

## 2. Handler 모델

```python
class UserHandlers:
    def __init__(self, client: ZLinkClient) -> None:
        self._client = client

    @zlink_request()
    async def get_user(
        self,
        request: GetUserRequest,
        context: ZLinkRequestContext,
    ) -> GetUserReply:
        account = await self._client.request(
            "account",
            GetAccountRequest(request.account_id),
        )
        return GetUserReply(request.account_id, account.nickname)
```

## 3. Dispatch 기준

- 일반 request/send dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 pending request의 reply correlation 경로다.
- 같은 outbound channel에서 discovery와 manual을 같이 섞지 않는다.

## 4. Outbound-only 앱

local handler 없이 outbound client만 쓰는 앱도 가능해야 한다.
이 경우 local `ROUTER(server)`는 열지 않고 outbound `DEALER(client)`만 만든다.

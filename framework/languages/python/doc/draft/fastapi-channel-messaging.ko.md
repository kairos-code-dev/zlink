<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Python Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: Draft -- ZLink Framework FastAPI Monitoring](./fastapi-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

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
    channels={
        "api": ChannelOptions(server={}),
        "profile": ChannelOptions(client=ClientCapabilityOptions()),
        "account": ChannelOptions(client=ClientCapabilityOptions()),
    },
    discovery=["tcp://registry1:5551", "tcp://registry2:5551"],
)
```

수동 연결은 아래처럼 둔다.

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

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.
같은 `profile` channel이라도 `profile.subscriber`는 별도 연결 집합으로 본다.
client manual 연결은 remote `target_rid`를 따로 받지 않고 endpoint 집합만 관리한다.

## 2. Handler 모델

일반 `PUB/SUB` event publish는 `ZLinkEventPublisher` 같은 별도 dependency로
설명하는 편이 맞다. 이 표면도 `channel_name + topic` 기준으로 동작한다.

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
- 같은 역할에서 discovery와 manual을 같이 섞지 않는다.
- manual 역할은 런타임 `connect`, `disconnect`, `list_connections`도 지원해야 한다.

## 4. Outbound-only 앱

local handler 없이 outbound client만 쓰는 앱도 가능해야 한다.
이 경우 local `ROUTER(server)`는 열지 않고 outbound `DEALER(client)`만 만든다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Python Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: Draft -- ZLink Framework FastAPI Monitoring](./fastapi-monitoring.ko.md)
<!-- framework-adapter-nav:bottom:end -->

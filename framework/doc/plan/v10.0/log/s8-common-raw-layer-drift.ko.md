# S8 공통 raw-layer 드리프트 추적 (mesh 전환과 별개)

Core 10.0.0은 Service(mesh) 계층뿐 아니라 일부 **raw socket 계층 심볼**도 변경/제거했다.
bindings는 9.0.4 raw 표면 가정으로 작성됐으므로 아래 드리프트를 4 lane에서 일관 흡수해야 한다.
mesh 전환 finding-ledger와 별개 트랙으로, 각 lane bindings 리뷰가 raw 계층까지 판정하면 병합한다.

## 확인된 드리프트

1. **`zlink_subscribe_handler` 제거** — Core 10.0.0 헤더·`.so` export 모두 없음. raw SUB 구독은
   `zlink_set_subscription`/`zlink_unset_subscription`/`zlink_subscription_at`/`zlink_subscribe_part`/
   `zlink_xpub_recv_part` 모델. dotnet은 `SubscribeHandler`를 16회 사용(런타임 파손). 다른 lane도
   raw SUB에서 동형 사용 가능성. → 각 lane raw SUB를 set_subscription 모델로 정렬.
2. **`zlink_router_recv_part` 시그니처 변경**(spot_rid out-param 제거) — cpp·node lane이 이미 수정.
3. **`zlink_router_*_spot_part` / router-direct spot 제거** — cpp·dotnet·node·jvm lane에서 제거 진행.
4. **미사용 dead P/Invoke 선언**(Core 미export, 호출 0): `zlink_stream_attach_raw`,
   `zlink_stream_detach`, `zlink_poller_wait_pinned` — dotnet에 선언만 잔존. 각 lane에서 제거.

## 처리
각 lane bindings 리뷰(raw 계층 포함)가 finding으로 확정하면 lane fix에 포함. subscribe_handler는
기능 재배선이 필요하므로 lane별 raw SUB 경로 수정. 3·4는 단순 제거.

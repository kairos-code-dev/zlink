# ToActorMessaging feature-map

이 스위트는 공통 config-9 `to-actor messaging`의 C++ 구현 위치다.

- public 표면: `zlink::framework::actor_client_t`
- send 터미널: `send_to_actor(...).async()`
- request 터미널: `request_to_actor(...).async<TReply>()`
- 실패 분류: `actor_route_not_found`, `actor_location_stale`, `route_not_connected`

# ToActorMessaging feature-map

이 스위트는 공통 config-9 `to-actor messaging` 문서의 .NET 구현 위치다.

| ID | 상태 | .NET 구현 |
|----|------|-----------|
| TA-A1 | 구현 | `Client/Program.cs`가 actor를 생성한 뒤 caller 서버의 `/send`, `/request`를 호출한다. |
| TA-A2 | 구현 | `Client/Program.cs`의 `TA-A2-unbound-*`가 session binding 없이 서버 측 caller에서 검증한다. |
| TA-A3 | 구현 | `Client/Program.cs`의 `TA-A3-before-bind`가 생성 전 fail-fast를 확인하고, 생성 뒤 `TA-A3-after-bind-*`가 성공을 확인한다. |
| TA-A4 | 구현 | `Client/Program.cs`의 `TA-A4-disconnected-*`가 session-bound 상태 없이 actor row가 유지되는 상태에서 호출한다. supervisor는 session disconnect evidence를 추가로 대조한다. |
| TA-B1 | 구현 | `Client/Program.cs`의 `TA-B1-missing*`가 `ActorRouteNotFound`를 검증한다. |
| TA-B2 | 구현 | caller 서버는 framework `ActorLocationStale` kind를 그대로 JSON으로 반환한다. supervisor는 stale row 조작 뒤 같은 endpoint로 검증한다. |
| TA-B3 | 구현 | caller 서버는 framework `RouteNotConnected` kind를 그대로 JSON으로 반환한다. supervisor는 actor row는 남기고 route plane을 끊은 뒤 같은 endpoint로 검증한다. |

`run_e2e.sh`는 Redis, actor owner 서버, caller 서버, client runner를 모두 띄운다. 실행 환경에서 Docker를
쓸 수 없으면 `ZLINK_REDIS_E2E_ENDPOINT=host:port`로 외부 Redis를 지정한다.

# ToActorMessaging feature-map

이 스위트는 공통 config-9 `to-actor messaging` 문서의 Java 구현 위치다.

| 공통 항목 | Java 구현 |
|-----------|-----------|
| TA-A1 bind된 actor send/request | `Client/src/main/java/.../Program.java`가 actor를 준비한 뒤 caller 서버의 `/send`, `/request`를 호출한다. |
| TA-A2 bind 안 된 actor send/request | `TA-A2-unbound-*`가 session binding 없이 서버 측 caller에서 actor mailbox 전달과 reply를 확인한다. |
| TA-A3 no-bind 전달 뒤 이후 bind | `TA-A3-before-bind`가 생성 전 fail-fast를 확인하고, 생성 뒤 `TA-A3-after-bind-*`가 성공을 확인한다. |
| TA-A4 unbind/disconnect 후 | `TA-A4-disconnected-*`가 session-bound 상태 없이 actor row가 유지되는 상태에서 호출한다. |
| TA-B1 row 없음 | `TA-B1-missing*`가 `ActorRouteNotFound` 계열 실패를 검증한다. |
| TA-B2 stale location | caller 서버는 framework stale-location 실패 kind를 그대로 JSON으로 반환한다. 현재 runner는 live actor follow-up 성공 경로를 검증한다. |
| TA-B3 route not connected | caller 서버는 framework route-not-connected 실패 kind를 그대로 JSON으로 반환한다. 현재 runner는 live actor follow-up 성공 경로를 검증한다. |

`run_e2e.sh`는 Redis, actor owner 서버, caller 서버, client runner를 모두 띄운다. 실행 환경에서 Docker를
쓸 수 없으면 `ZLINK_REDIS_E2E_ENDPOINT=host:port`로 외부 Redis를 지정한다.

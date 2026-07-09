# ToActorMessaging feature-map

이 스위트는 공통 config-9 `to-actor messaging` 문서의 Java 구현 위치다.

| 공통 항목 | Java 구현 |
|-----------|-----------|
| TA-A1 bind된 actor send/request | `Client/src/main/java/.../Program.java`가 actor를 준비한 뒤 caller 서버의 `/send`, `/request`를 호출한다. |
| TA-A2 bind 안 된 actor send/request | `TA-A2-unbound-*`가 session binding 없이 서버 측 caller에서 actor mailbox 전달과 reply를 확인한다. |
| TA-A3 no-bind 전달 뒤 이후 bind | `TA-A3-before-bind`가 생성 전 fail-fast를 확인하고, 생성 뒤 `TA-A3-after-bind-*`가 성공을 확인한다. |
| TA-A4 unbind/disconnect 후 | `TA-A4-disconnected-*`가 session-bound 상태 없이 actor row가 유지되는 상태에서 호출한다. |
| TA-B1 row 없음 | `TA-B1-missing*`가 `ActorRouteNotFound` 계열 실패를 검증한다. |
| TA-B2 stale location | actor 서버가 public `ActorRef` 값을 wire DTO로 넘기고, caller 서버의 `/request-ref`가 같은 public ref 호출 경로에서 generation을 바꾼 stale ref를 호출해 `ACTOR_LOCATION_STALE`을 확인한다. 이후 actor id 기반 재조회 호출이 성공하는지도 확인한다. |
| TA-B3 route not connected | caller 서버의 `/request-ref`가 존재하지 않는 node RID를 가진 ref를 호출해 `ROUTE_NOT_CONNECTED`를 확인한다. 이후 정상 actor id 기반 호출이 성공하는지도 확인한다. |

`run_e2e.sh`는 Redis, actor owner 서버, caller 서버, client runner를 모두 띄운다. 실행 환경에서 Docker를
쓸 수 없으면 `ZLINK_REDIS_E2E_ENDPOINT=host:port`로 외부 Redis를 지정한다.

client runner는 caller 응답만 보지 않고 actor 서버의 `/evidence`도 읽는다. 이 확인은 성공 scenario가
실제 actor handler까지 도달했는지, 그리고 TA-B1 missing actor 호출이 actor handler에 도달하지 않았는지
같이 검증한다.

최근 검증: `nice -n 10 timeout 600s ./run_e2e.sh all` 실행 결과
`logs/20260707-221746-3642522`에서 `to-actor-messaging e2e result=passed`를 확인했다.

# ToActorMessaging feature-map

이 디렉토리는 공통 config-9 `to-actor messaging` 문서의 Kotlin 구현 범위를 기록한다.

| 공통 항목 | Kotlin 구현 |
|-----------|-------------|
| TA-A1 bind된 actor send/request | `Client/src/main/java/.../Program.java`가 actor를 준비한 뒤 caller 서버의 `/send`, `/request`를 호출한다. Caller role은 one-way call의 public `submit()`과 request의 Kotlin public `awaitReply` extension을 사용한다. |
| TA-A2 bind 안 된 actor send/request | `TA-A2-unbound-*`가 session binding 없이 서버 측 caller에서 actor mailbox 전달과 reply를 확인한다. |
| TA-A3 no-bind 전달 뒤 이후 bind | `TA-A3-before-bind`가 생성 전 fail-fast를 확인하고, 생성 뒤 `TA-A3-after-bind-*`가 성공을 확인한다. |
| TA-A4 unbind/disconnect 후 | `TA-A4-disconnected-*`가 session-bound 상태 없이 actor row가 유지되는 상태에서 호출한다. |
| TA-B1 row 없음 | 차단. shared Java runtime의 missing route 분류가 공통 계약과 다르므로 현재 `TA-B1-missing*` 결과를 완료 증거로 사용하지 않는다. runtime 오류 분류를 수정한 뒤 actor handler 미도달과 정확한 public error를 함께 검증해야 한다. |
| TA-B2 stale location | Actor owner 서버가 public location store API로 stale actor row를 만들고, client가 caller 서버의 `ACTOR_LOCATION_STALE` 반환, fault handler evidence 없음, 복구 뒤 request 성공을 확인한다. |
| TA-B3 route not connected | Actor owner 서버가 public location store API로 연결되지 않은 routing id를 가진 actor row를 만들고, client가 caller 서버의 `ROUTE_NOT_CONNECTED` 반환, fault handler evidence 없음, 복구 뒤 request 성공을 확인한다. |

`run_e2e.sh`는 Redis, actor owner 서버, Kotlin caller 서버, client runner를 모두 시작한다. Runner는
실행마다 전용 Docker Redis container를 만들며, container를 만들지 못하면 즉시 실행을 실패로
종료한다.

최신 검증: 2026-07-07 현재 checkout에서
`nice -n 10 timeout 420s ./run_e2e.sh`가 통과했다. 증거 로그는
`logs/20260707-172116-2831364/client.log`이고, 최종 marker는
`to-actor-messaging e2e result=passed`다.

## 검증 메모

fault control은 Redis 키를 직접 조작하지 않는다. Actor owner 서버가 framework public location store
API로 actor row를 갱신하고, Kotlin caller 서버는 public `ZLinkActorClient`와 coroutine await extension
으로 send/request를 실행한다. Client runner는 fault scenario 이름이 actor evidence에 남지 않는지 확인해
stale descriptor와 disconnected route 상태에서 actor handler가 실행되지 않았음을 검증한다.

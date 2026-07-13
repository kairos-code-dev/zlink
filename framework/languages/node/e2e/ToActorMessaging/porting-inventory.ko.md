# ToActorMessaging Node 포팅 인벤토리

이 문서는 공통 config-9 `to-actor messaging` 시나리오를 Node E2E 구현과 runner 증거에 매핑한다.

| 공통 ID | 상태 | Node 구현 | Runner 증거 |
|---------|------|-----------|-------------|
| TA-A1 | implemented | `Client/main.ts`가 session stream으로 actor를 bind하고 relay push를 확인한 뒤 caller 서버의 `/send`, `/request`로 no-bind 전달이 기존 bind를 오염시키지 않는지 검증한다. | `run_e2e.sh`가 actor/session/caller/client를 실행하고 `to-actor-messaging e2e result=passed`를 출력한다. |
| TA-A2 | implemented | `Client/main.ts`의 `TA-A2-unbound-*` 흐름이 session binding 없이 actor ref 기반 send/request를 검증한다. | `run_e2e.sh` client stdout에 scenario marker와 최종 pass marker가 남는다. |
| TA-A3 | implemented | `TA-A3-before-bind-*`는 session bind 전 no-bind send/request를 먼저 확인하고, 이후 stream bind와 relay push가 같은 actor에서 성공하는지 확인한다. | actor evidence와 client stdout을 같은 run log 아래에 남긴다. |
| TA-A4 | implemented | `TA-A4-disconnected-*`가 stream bind를 닫은 뒤 actor row가 유지되는 상태의 send/request를 검증한다. | actor/session/caller 서버 로그와 client stdout이 runner log directory에 저장된다. |
| TA-B1 | implemented | `TA-B1-missing*`가 없는 actor에 대해 `actorRouteNotFound`를 검증한다. | client runner가 error kind를 확인한 뒤 pass marker를 출력한다. |
| TA-B2 | implemented | 정상 actor snapshot에서 generation만 오래된 값으로 바꿔 stale actor 동작을 검증한다. | client runner가 one-way send의 로컬 제출 완료와 handler evidence 부재를 확인하고, request의 `actorLocationStale` 실패 kind를 확인한다. |
| TA-B3 | implemented | actor row는 만들고 연결되지 않은 node rid를 가진 actor snapshot으로 route 미연결 동작을 검증한다. | client runner가 one-way send의 로컬 제출 완료와 handler evidence 부재를 확인하고, request의 `routeNotConnected` 실패 kind를 확인한다. |

`run_e2e.sh`는 HTTP health만으로 client를 시작하지 않는다. Redis location store에서 `to-actor` SpotMesh의
actor/caller router와 pubsub endpoint row, owner lease를 확인한 뒤 client를 실행한다. 이 readiness 확인은
startup topology 검증이며 scenario 실패를 retry로 숨기기 위한 우회가 아니다.

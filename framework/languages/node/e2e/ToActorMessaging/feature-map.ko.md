# ToActorMessaging feature-map

이 스위트는 공통 config-9 `to-actor messaging` 문서의 Node 구현 위치다.

| 공통 ID | 상태 | Node 구현 | Runner 증거 |
|---------|------|-----------|-------------|
| TA-A1 | implemented | `Client/main.ts`의 `runTaA1()`이 session stream으로 actor를 bind하고 stream relay push를 확인한 뒤, caller 서버의 `/send`, `/request`로 no-bind 전달이 기존 session bind를 오염시키지 않는지 검증한다. | `run_e2e.sh TA-A1` 또는 전체 실행에서 `scenario TA-A1 passed`와 `to-actor-messaging e2e result=passed`를 출력한다. |
| TA-A2 | implemented | `runTaA2()`가 session binding 없이 actor ref 기반 send/request를 검증한다. | `run_e2e.sh TA-A2` 또는 전체 실행에서 `scenario TA-A2 passed`를 출력한다. |
| TA-A3 | implemented | `runTaA3()`가 session bind 전 no-bind send/request를 먼저 확인하고, 이후 stream bind와 relay push가 같은 actor에서 성공하는지 확인한다. | `run_e2e.sh TA-A3` 또는 전체 실행에서 `scenario TA-A3 passed`를 출력한다. |
| TA-A4 | implemented | `runTaA4()`가 stream bind를 닫은 뒤 actor row가 유지되어 no-bind send/request가 성공하는지 확인한다. | `run_e2e.sh TA-A4` 또는 전체 실행에서 `scenario TA-A4 passed`를 출력한다. |
| TA-B1 | implemented | `runTaB1()`이 없는 actor에 대해 send/request 모두 `actorRouteNotFound`를 검증한다. | `run_e2e.sh TA-B1` 또는 전체 실행에서 `scenario TA-B1 passed`를 출력한다. |
| TA-B2 | implemented | `runTaB2()`가 정상 actor snapshot에서 generation만 오래된 값으로 바꾼다. one-way send는 로컬 제출 완료 뒤 handler evidence가 없음을 확인하고, request는 `actorLocationStale` 실패를 확인한다. | `run_e2e.sh TA-B2` 또는 전체 실행에서 `scenario TA-B2 passed`를 출력한다. |
| TA-B3 | implemented | `runTaB3()`가 actor row는 만든 뒤 연결되지 않은 node rid를 가진 actor snapshot을 사용한다. one-way send는 로컬 제출 완료 뒤 handler evidence가 없음을 확인하고, request는 `routeNotConnected` 실패를 확인한다. | `run_e2e.sh TA-B3` 또는 전체 실행에서 `scenario TA-B3 passed`를 출력한다. |

`run_e2e.sh`는 Redis, actor owner 서버, session stream 서버, caller 서버, client runner를 모두 띄운다.
서버 역할은 `E2E_START_ORDER=reverse`와 고정 seed `shuffle:20260715`로도 시작할 수 있으며, 두
변형의 `TA-A1` runner가 통과했다.
인자를 주지 않으면 전체 scenario를 실행하고, `TA-B2`처럼 공통 ID를 첫 인자로 주면 해당 scenario만
실행한다. location store는 실행마다 Docker로 전용 Redis container를 만들며 다른 실행이나 host의
Redis 인스턴스를 공유하지 않는다.

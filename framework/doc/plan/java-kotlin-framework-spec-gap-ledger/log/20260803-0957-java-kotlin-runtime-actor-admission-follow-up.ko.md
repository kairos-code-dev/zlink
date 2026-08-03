# Java/Kotlin runtime actor admission follow-up

실행 시각: 2026-08-03 09:57 KST

이번 작업은 Java core runtime이 target Actor를 registry에 공개한 뒤
`onJoinedActor` callback이 끝나기 전 도착한 일반 Actor request를 처리한 문제를
수정하고, 같은 admission 경계를 Java/Kotlin 공통 runtime test와 process에서 다시
확인한 기록이다.

## 원인과 수정

`ZLinkSpotRuntime.dispatchLocalActorPacket`은 Actor가 `moving` 상태일 때
`captureMovingPacket`을 호출한다. 일반 request는 bound-session relocation journal을
만들 수 없어 `null`을 반환할 수 있는데, 이 결과를 move completion 대기로 전환하지
않고 바로 handler dispatch로 진행했다. 같은 조건에서
`ZLinkActorSessionCoordinator.dispatchLocalSession`은 이미 move completion을
기다리는 경로를 사용하고 있었다.

수정한 책임 경계는 다음과 같다.

- `ZLinkActorSessionCoordinator.awaitMoveCompletion`을 통해 Actor runtime의
  admission completion stage를 사용한다.
- local Spot Actor packet이 moving 중이고 handoff capture 결과가 `null`이면
  move completion 뒤에 기존 `enqueueLocalActorPacket` 경로를 실행한다.
- header, payload와 inbound dispatch lease는 deferred stage가 terminal이 될 때
  한 번만 release한다.
- client나 sample에 재시도, raw frame 처리, private API 우회를 추가하지 않았다.

변경 파일:

- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/spots/ZLinkSpotRuntime.java`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/spots/ZLinkActorSessionCoordinator.java`
- `framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/actors/ZLinkActorRelocationStagingTest.java`
- `framework/languages/java/e2e/SpotActorTransfer/Client/src/main/java/systems/zlink/e2e/spotactortransfer/client/Program.java`
- `framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh`

## 검증 결과

| 구분 | 명령 또는 evidence | 결과 |
|------|--------------------|------|
| focused unit | `./gradlew --no-daemon --no-parallel --max-workers=5 :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorRelocationStagingTest` | PASS, exit 0 |
| Java/Kotlin runtime module | `./gradlew --no-daemon --no-parallel --max-workers=5 --rerun-tasks :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-kotlin:contractTest :zlink-stream-connector:test :zlink-http-client:test :zlink-http-client-kotlin:test` | PASS, 37 actionable tasks, exit 0 |
| API snapshot | `framework/languages/java/scripts/verify_api_snapshot.sh java|kotlin` | PASS, exit 0 |
| packaged consumer | `framework/languages/java/scripts/verify_packaged_contract.sh java|kotlin` | PASS, exit 0 |
| ST-F1 | `framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh ST-F1 --start-order forward` | PASS, `log/20260803-094952-558568`; target evidence가 `joined_wait`, `joined_released`, `target_backlog_replayed`, `P1`, `P2`, `P3` 순서로 확인됨 |
| ST-F2 | `framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh ST-F2 --start-order forward` | PASS, `log/20260803-095035-560660`; moving request `B1`, `B2`가 direct `D1`보다 먼저 처리됨 |
| ST-F6 | `framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh ST-F6 --start-order reverse` | PASS, `log/20260803-095444-580496`; correlated reply, 원래 timeout, late handler 1회와 source 중복 dispatch 부재를 확인함 |

`ST-F1`, `ST-F2`, `ST-F6`의 fixture는 target Actor가 materialize된 뒤 callback gate에서
대기하는 조건에서 target public HTTP endpoint를 사용한다. source endpoint의 stale
location lookup을 성공으로 세지 않으며, dynamic owner evidence를 사용해 `actor-a`/
`actor-b`/`actor-c` 고정 가정을 제거했다.

## 현재 판정

- JK-IMP-005의 일반 local Spot Actor request admission 누락은 production fix와 unit
  regression으로 닫혔다.
- Java와 Kotlin은 같은 Java core runtime 경로를 사용하므로 Kotlin production source의
  별도 workaround는 추가하지 않았다.
- JK-IMP-005 전체 완료는 아직 아니다. CAS loser, stale generation, cleanup failure,
  restart/takeover, routed transfer와 role-level authority evidence는 남아 있다.
- Config 10/13/14 aggregate, sample process, CI와 전체 E2E selector는 이번 runtime
  수정의 완료 근거로 계산하지 않는다.

참고로 repository root에서 `./scripts/verify_api_snapshot.sh`를 실행한 첫 시도는
script가 `framework/languages/java/scripts/` 아래에 있어 exit 127을 반환했다. 실제
지정 경로의 재실행은 위 표처럼 모두 성공했다.

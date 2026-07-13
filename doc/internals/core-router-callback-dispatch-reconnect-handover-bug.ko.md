# Core ROUTER callback dispatch 재연결 handover 버그

## 상태

- 확인일: 2026-07-13
- 상태: core 9.0.2에서 수정 완료
- 발견 경로: Java framework `AutomaticTurnDispatch` Config 8의 서버 종료·재기동 검증
- 영향 범위: socket message callback dispatch가 설정된 ROUTER가 같은 routing ID로 재연결하는 경로

이 문서는 확인된 core 버그의 원인, 기존 수정의 한계와 최종 수정 내용을 정리한다. Java/Kotlin framework는
재시도나 구동 순서 조정으로 문제를 우회하지 않고 framework가 소유한 ROUTER에 handover 정책을
내부적으로 적용한다.

## 현상

ROUTER peer가 종료된 뒤 같은 endpoint와 routing ID로 다시 시작하면 TCP 연결은 다시 설정되지만,
요청이 새 pipe로 전달되지 않는다. 기존 pipe가 제거될 때까지 약 30초가 지난 뒤에야 요청이 처리된다.

Java Config 8에서는 다음 순서로 재현됐다.

1. `Session` ROUTER와 `play-a` ROUTER 사이에서 요청과 응답을 여러 번 주고받는다.
2. `play-a`를 종료한다.
3. 같은 endpoint와 `play-a` routing ID로 서버를 다시 시작한다.
4. `Session`에서 `play-a`로 route request를 보낸다.
5. 새 TCP 연결이 설정되어 있어도 요청이 처리되지 않고 30초 timeout 경계까지 지연된다.

애플리케이션의 서버 구동 순서를 바꾸거나 재시도 횟수를 늘려 해결할 문제가 아니다. framework가
사용하는 callback dispatch 경로에서도 core가 새 pipe를 즉시 채택해야 한다.

## 원인

문제 코드는 `core/src/runtime/sockets/router/router_admission.cpp`에 있던
`router_t::duplicate_pipe_should_replace(...)`에 있었다. commit `414554f24`는 같은 방향의 duplicate가
항상 최신 pipe를 채택하도록 고쳤지만, 실제 framework 재기동에서는 기존 pipe와 새 pipe의 생성 방향이
달라지는 경우도 확인됐다. 따라서 같은 방향만 처리한 현재 수정으로는 생명주기 결함이 닫히지 않는다.

같은 방향으로 만들어진 기존 pipe와 새 pipe의 routing ID가 같을 때 현재 구현은 다음 조건을 적용한다.

```cpp
if (!socket_msg_dispatch_active ())
    return true;
return existing_outpipe_.pipe
       && existing_outpipe_.pipe->get_msgs_read () == 0
       && existing_outpipe_.pipe->get_msgs_written () == 0;
```

callback dispatch가 없으면 새 pipe를 채택한다. callback dispatch가 있으면 기존 pipe가 메시지를 한 번도
처리하지 않았을 때만 새 pipe를 채택한다. 정상 트래픽을 처리한 framework ROUTER의 기존 pipe는 항상
두 번째 조건에서 거부된다.

과거 메시지 처리 횟수는 기존 pipe가 현재 유효하다는 증거가 아니다. 서버 재시작으로 만들어진 새
pipe도 불필요한 중복 연결과 같은 방식으로 거부되기 때문에 handover 의미가 깨진다.

같은 방향 교체를 적용한 뒤 Kotlin `ATD-E3`와 `OBS-C2`에서 확인한 로그는 다음과 같다.

```text
router identify_peer: keep existing duplicate rid=play-a existing_local=0 new_local=1
router xsend_routed: no out pipe rid_size=6 rid=play-a
```

재기동 전 연결은 remote에서 시작한 pipe로 기록되어 있고, 재기동 뒤 새 연결은 local에서 시작한
pipe로 들어온다. 현재 코드는 이 경우 routing ID 비교로 승자를 고르므로 새 pipe를 거부한다. 이어서
기존 pipe도 종료되면 routing ID에 대응하는 출력 pipe가 없어지고, 다음 handshake timeout 경계까지
route request가 처리되지 않는다.

Java 전체 runner에서도 같은 재기동 단계가 차단됐다. 2026-07-13 실행 로그
`framework/languages/java/e2e/AutomaticTurnDispatch/logs/20260713-151721-1434501/`에서는
ATD-A1~E2, E4, E5와 종료 대기가 통과한 뒤 `play-a`를 같은 routing ID로 재기동했다. 재기동
프로세스와 HTTP endpoint가 준비된 뒤에도 route/Spot readiness가 16회 연속 실패했다. runner의
재시도 상한이 약 20분이어서 원인 로그를 확보한 뒤 실행을 중단했다. 이 결과는 일반 시나리오나
애플리케이션 구동 순서 문제가 아니라 동일 routing ID 재기동 경로에 국한된다는 점을 다시 확인한다.

## 기존 테스트가 놓친 이유

다음 기존 테스트는 일반 handover와 connect routing ID 중복 정책을 검증하며 현재 상태에서 통과한다.

- `core/build/bin/test_connect_rid`: 7개 테스트 통과
- `core/build/bin/test_router_handover`: 2개 테스트 통과

하지만 이 테스트들은 framework와 같은 socket message callback dispatch를 활성화한 상태에서,
메시지를 처리한 기존 pipe를 둔 채 같은 방향·같은 routing ID로 재연결하는 조합을 검증하지 않는다.
따라서 `socket_msg_dispatch_active()` 이후의 문제 분기가 검증 범위에서 빠져 있다.

## 적용한 수정

handover 정책이 활성화된 상태에서 같은 방향·같은 routing ID의 재연결이 들어오면, 기존 pipe의 과거
메시지 처리 횟수만으로 새 pipe를 거부하면 안 된다. 서버 재시작 후 새 연결이 기존 연결을 즉시
대체할 수 있도록 pipe 생명주기와 중복 연결 판단을 분리해야 한다.

다음 두 대안을 비교했다.

1. handover가 활성화된 duplicate는 생성 방향과 관계없이 항상 최신 pipe로 교체한다.
2. 기존 pipe의 실제 연결 상태와 새 연결의 endpoint·세대 정보를 확인해 stale pipe일 때만 교체한다.

첫 번째 안을 적용했다. 기존 ROUTER handover 계약은 중복 routing ID가 들어오면 새 pipe가 identity를
인수하고 기존 pipe를 종료한다고 이미 정의한다. 연결 방향이나 routing ID 정렬로 다시 승자를 고르면
이 계약과 충돌하고, 애플리케이션이 서버 구동 순서를 알아야 하는 문제가 다시 생긴다. 두 번째 안은
연결 세대와 종료 상태를 admission 계층에 새로 전달해야 하므로 내부 상태와 시간 순서 의존성을 늘린다.

따라서 `duplicate_pipe_should_replace(...)`를 제거하고 handover가 활성화된 중복 routing ID는 생성
방향과 과거 메시지 통계에 관계없이 새 pipe가 인수하도록 `adopt_peer_routing_id(...)` 한 곳에서
처리했다. handover 비활성 상태에서는 기존처럼 새 pipe를 거부한다.

수정 뒤 같은 방향뿐 아니라 `existing_local=0, new_local=1` 재기동 조합도 새 pipe를 즉시 채택한다.
동시에 연결한 정상 peer의 결정적 승자 선택과 handover 비활성 duplicate 거부 계약은 별도 테스트로
유지해야 한다. framework에서 sleep, 재시도 횟수 증가, 서버 구동 순서 고정으로 우회하지 않는다.

Java framework는 ROUTER 생성 시 binding의 공개 옵션을 사용해 handover를 활성화한다. 이 설정은
framework 내부 연결 정책이므로 애플리케이션 개발자가 서버 구동 순서나 core 옵션을 알 필요가 없다.

## 회귀 테스트 요구 사항

core integration test에는 다음 조건을 포함하는 시나리오가 필요하다.

1. ROUTER에 socket message callback dispatch를 등록한다.
2. 같은 routing ID의 peer와 연결해 최소 한 번 요청과 응답을 처리한다.
3. 기존 pipe가 유지된 상태에서 같은 방향과 routing ID의 새 pipe를 만든다.
4. 새 pipe가 채택되어 새 peer가 메시지를 수신하는지 확인한다. 새 pipe의 attach는 비동기로
   일어나므로, 고정 대기 후 한 번만 보내는 대신 5초 상한 안에서 메시지를 반복 전송하며 새
   peer의 수신 여부를 확인한다. 이렇게 하면 attach 시점이 늦어지는 부하 환경에서도 테스트가
   admission 판정만 검증한다.
5. 수정 전 조건을 복원하면 새 pipe가 계속 거부되어 모든 반복 전송이 기존 peer로만 전달되고,
   이 테스트가 5초 상한을 소진한 뒤 실패하는지 확인한다.
6. 기존 pipe가 remote 시작이고 새 pipe가 local 시작인 재기동 조합에서도 새 pipe가 즉시 채택되는지
   확인한다.
7. 양쪽 peer가 동시에 연결을 시작한 정상 중복 연결은 기존 결정 규칙에 따라 한 pipe로 수렴하는지
   확인한다.

같은 endpoint에서 실제 서버를 종료하고 같은 routing ID로 재기동하는 전체 생명주기는 Java Config 8의
`ATD-E3`에서 검증한다. core 테스트는 admission 조건을 직접 고정하고, E2E는 실제 배포 구성을 검증한다.

수정 후 다음 명령을 모두 실행해 검증한다.

```bash
cmake --build core/build
core/build/bin/test_connect_rid
core/build/bin/test_router_handover
cd framework/languages/java/e2e/AutomaticTurnDispatch
./run_e2e.sh all
```

완료 조건은 다음과 같다.

- `test_router_handover`의 기존 시나리오와 callback dispatch 재연결 회귀 시나리오가 모두 통과한다.
- `test_connect_rid`의 기존 7개 시나리오가 모두 통과한다.
- Java `AutomaticTurnDispatch ./run_e2e.sh all`에서 `ATD-A1`부터 `ATD-E5`까지 통과한다.
- `ATD-E3`에서 종료 대기와 동일 routing ID 재기동 복구가 30초 handshake timeout을 기다리지 않고
  완료된다.
- Kotlin `AutomaticTurnDispatch ./run_e2e.sh ATD-E3`와 Kotlin `ObservabilityOps ./run_e2e.sh OBS-C2`가
  서버 구동 순서를 조정하지 않고 통과한다.

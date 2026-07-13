# Core ROUTER callback dispatch 재연결 handover 버그

## 상태

- 확인일: 2026-07-13
- 상태: 수정 및 회귀 검증 완료
- 발견 경로: Java framework `AutomaticTurnDispatch` Config 8의 서버 종료·재기동 검증
- 영향 범위: socket message callback dispatch가 설정된 ROUTER가 같은 routing ID로 재연결하는 경로

이 문서는 확인된 core 버그의 원인, 수정 내용, 회귀 검증 결과를 정리한다. Java/Kotlin framework는
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

문제 코드는 `core/src/runtime/sockets/router/router_admission.cpp`의
`router_t::duplicate_pipe_should_replace(...)`에 있다.

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

1. 같은 방향의 duplicate pipe는 handover 설정에 따라 항상 최신 pipe로 교체한다.
2. 기존 pipe의 실제 연결 상태와 새 연결의 endpoint·세대 정보를 확인해 stale pipe일 때만 교체한다.

첫 번째 안을 선택했다. handover는 같은 routing ID의 새 pipe가 기존 pipe를 대체하도록 요청하는
정책이므로, 과거 메시지 통계로 이를 다시 거부하면 정책 의미가 일관되지 않는다. 두 번째 안은 연결
세대 추적을 새로 도입해야 하고 현재 공개 계약에 없는 상태를 여러 계층이 공유하게 된다.

`duplicate_pipe_should_replace(...)`는 handover가 활성화된 같은 방향의 duplicate에 대해 새 pipe를
채택한다. 서로 반대 방향에서 동시에 연결한 경우의 routing ID 비교와 handover 비활성 duplicate 거부
계약은 그대로 유지한다.

Java framework는 ROUTER 생성 시 binding의 공개 옵션을 사용해 handover를 활성화한다. 이 설정은
framework 내부 연결 정책이므로 애플리케이션 개발자가 서버 구동 순서나 core 옵션을 알 필요가 없다.

## 회귀 테스트

core integration test에는 다음 조건을 포함하는 시나리오를 추가했다.

1. ROUTER에 socket message callback dispatch를 등록한다.
2. 같은 routing ID의 peer와 연결해 최소 한 번 요청과 응답을 처리한다.
3. 기존 pipe가 유지된 상태에서 같은 방향과 routing ID의 새 pipe를 만든다.
4. 새 pipe가 즉시 채택되어 새 peer가 메시지를 수신하는지 확인한다.
5. 수정 전 조건을 복원하면 이 테스트가 5초 수신 상한에서 `EAGAIN`으로 실패하는지 확인한다.

같은 endpoint에서 실제 서버를 종료하고 같은 routing ID로 재기동하는 전체 생명주기는 Java Config 8의
`ATD-E3`에서 검증한다. core 테스트는 admission 조건을 직접 고정하고, E2E는 실제 배포 구성을 검증한다.

다음 검증을 수행했다.

```bash
cmake --build core/build
core/build/bin/test_connect_rid
core/build/bin/test_router_handover
cd framework/languages/java/e2e/AutomaticTurnDispatch
./run_e2e.sh all
```

- `test_router_handover`: 3개 테스트 통과
- `test_connect_rid`: 7개 테스트 통과
- Java `AutomaticTurnDispatch ./run_e2e.sh all`: `ATD-A1`부터 `ATD-E5`까지 통과
- `ATD-E3`: 종료 대기와 동일 routing ID 재기동 복구 통과

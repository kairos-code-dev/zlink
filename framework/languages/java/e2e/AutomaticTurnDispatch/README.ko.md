# Java AutomaticTurnDispatch E2E

이 fixture는 공통 E2E Config 8의 `ATD-A1`부터 `ATD-E5`까지 검증한다. consumer는 실제
stream connector로 session gateway에 요청을 보내며, gateway는 route mesh를 통해 play node의
Spot과 actor로 요청을 전달한다. HTTP endpoint는 readiness와 evidence 조회에만 사용한다.

Java handler는 request, actor join, worker call이 반환하는 `CompletionStage` 하나만 완료 경로로
사용한다. framework `CancellationToken`, blocking `await`, 구형 `yield` 호출은 server fixture에
없다. 일반 send와 session reply는 one-way `submit()`으로 맡긴다.

## 역할

- `Server/Delay`: client-server channel의 지연 응답을 제공한다.
- `Server/Play`: 두 play node, Entry Spot, probe Spot, actor와 timer mailbox를 제공한다.
- `Server/Session`: stream session을 받고 route/Spot/actor 경로로 요청을 relay한다.
- `Client`: 시나리오 packet을 보내고 reply, push, public error를 검증한다.
- `Shared`: typed packet, handler와 evidence schema를 공유한다.

## 실행

```bash
./run_e2e.sh
```

runner는 전용 Redis key prefix를 사용하고 모든 role을 별도 process로 실행한다. full 실행은
ATD-E3의 정상 종료와 같은 routing id 재사용까지 포함한다. 결과는 `logs/<run-id>/` 아래 role별
로그, evidence JSON, `automatic-turn-dispatch-marker-report.json`으로 남는다. report에는 19개
시나리오 ID와 공통 marker가 모두 있어야 성공한다.

focused 실행은 다음처럼 시나리오 ID를 전달한다.

```bash
./run_e2e.sh ATD-B2
```

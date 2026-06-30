# Java YieldDispatch E2E

이 디렉터리는 공통 E2E Config 8 Spot yield dispatch 시나리오를 Java framework에서 검증하는
작업 위치다.

현재 구현은 stream connector가 session gateway에 request packet을 보내고, session role이 route
mesh를 통해 play role의 target spot으로 scenario request를 전달하는 경로를 사용한다. `YD-A1`은
spot handler가 일반 `await`로 delay service reply를 기다리는 동안 probe가 뒤에 남는 순서를
검증한다. `YD-A2`는 같은 위치에서 `yield`를 사용했을 때 probe가 먼저 처리되고, delay reply 뒤에
원래 handler가 이어지는 순서를 검증한다. `YD-A3`는 stream metadata로 target spot을 고르고,
yield 전후 marker가 같은 request id, spot rid, correlation id를 유지하는지 검증한다. `YD-A4`는
framework worker pool에 맡긴 작업을 `yield`로 기다리는 동안 같은 Spot의 독립 probe가 먼저
처리되는지 검증한다. `YD-B1`은 actor A를 target spot에 join한 뒤 actor A가 delay service reply를
`yield`로 기다리는 동안, 같은 target spot에 join한 actor B의 빠른 actor request가 actor A의 reply보다
먼저 완료되는지 검증한다. `YD-B2`는 target spot에 join한 actor A가 `yield` 중일 때 같은 actor A의
빠른 request가 actor A의 continuation과 completion 뒤에 처리되는지 검증한다. `YD-B3`는 actor A의 Entry Spot actor handler가 actor join call을 `yield`로 기다리는 동안 actor B의
빠른 Entry Spot actor request가 먼저 완료되는지 검증한다. `YD-C1`은 yield 중인 timer와 빠른 timer를
같은 target spot에 함께 두고, yield 중인 timer가 reply를 기다리는 동안 빠른 timer tick이 먼저 완료되는지
검증한다. `YD-C2`는 같은 timer의 다음 tick이 이전 tick의 yield continuation과 completion 뒤에
실행되는지 검증한다. `YD-C3`는 actor가 `yield` 중일 때 timer가 진행되고, timer가 `yield` 중일 때
다른 actor가 진행되는지 검증한다. `YD-D2`는 `play-a`의 owner spot handler가 `play-b`의 target spot
reply를 `yield`로 기다린 뒤 원래 owner spot에서 재개되는지 검증한다. `YD-D3`는 session gateway가
route mesh로 보낸 packet이 `play-b` target spot handler에 도착한 뒤, 그 handler가 `yield` 중일 때 같은
target spot의 probe가 먼저 처리되는지 검증한다. `YD-D4`는 stream session relay로 bound actor
handler에 들어간 request가 `yield` 중일 때 bound session push를 원래 stream connector로 돌려보내고,
다른 actor의 push wait는 진행되지 않는지 검증한다. `YD-E1`은 timeout 뒤 같은 Spot mailbox가
post-timeout probe를 처리하는지 검증한다.

최근 full runner는 `logs/20260630-111601-3718472`에서 YD-A/B/C/D/E1과 E4 정적 검증을 통과했다.
shutdown recovery diagnostic은 `ZLINK_JAVA_E2E_RUN_E3_SHUTDOWN=1`로 실행할 수 있지만,
`logs/20260630-113136-3782250`에서 pending yield 중 play-a가 SIGTERM으로 내려가지 않고 client가
closed/cancelled public error 대신 request timeout을 받는 gap을 확인했다. 아직 cancellation,
shutdown recovery, cross-language marker report 비교 scenario는 완료하지 않았다.
남은 gap은 `feature-map.ko.md`와 `porting-inventory.ko.md`에 기록한다.

## 목표 역할

- `Shared`: scenario request/reply, evidence, stream packet 타입.
- `Server/Registry`: embedded registry process.
- `Server/Delay`: yield로 기다릴 client-server channel delay service.
- `Server/Play`: route mesh와 spot mesh를 열고 yield probe spot을 만든다.
- `Server/Session`: stream connector session gateway와 route/spot bridge를 구성한다.
- `Client`: stream connector로 session gateway에 접속해 scenario packet을 보내는 consumer.

HTTP endpoint는 health와 evidence 조회에만 사용한다. yield 시나리오 시작은 공통 문서와 같이 stream
connector request 경로로 들어가야 한다.

## 실행

```bash
./run_e2e.sh
```

runner는 각 role을 별도 process로 시작하고 `logs/<run-id>/` 아래에 stdout, stderr, message flow,
play/session evidence JSON을 남긴다. 성공 기준은 `scenario YD-A1 passed`, `scenario YD-A2 passed`,
`scenario YD-A3 passed`, `scenario YD-A4 passed`, `scenario YD-B1 passed`,
`scenario YD-B2 passed`, `scenario YD-B3 passed`, `scenario YD-C1 passed`,
`scenario YD-C2 passed`, `scenario YD-C3 passed`, `scenario YD-D2 passed`,
`scenario YD-D3 passed`, `scenario YD-D4 passed`,
`scenario YD-E1 passed`, `yield-dispatch e2e result=passed` 출력과 message flow 로그 생성이다.
최근 재실행 로그는 `logs/20260630-111601-3718472`이며, 현재 기본 runner 범위는 통과한다. 전체 Config 8
포팅 완료에는 아직 YD-E2, YD-E3, YD-E5 gap 처리가 남아 있다.

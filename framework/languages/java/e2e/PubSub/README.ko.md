# Java PubSub E2E

이 E2E는 공통 Config 3 Pub/Sub 시나리오를 Java framework public API로 검증한다.

역할은 `.NET` E2E와 같은 의미로 나뉜다.

| 위치 | 역할 |
|------|------|
| `Shared/` | publish message, evidence record, channel 이름을 공유한다. |
| `Server/Registry/` | discovery registry를 실행하고 `/health`로 readiness를 제공한다. |
| `Server/Publisher/` | public `ZLinkFanoutClient`로 publish를 수행하고 HTTP endpoint로 client trigger를 받는다. |
| `Server/Subscriber/` | publish handler, dispatch error observer, evidence endpoint를 제공한다. |
| `Client/` | PS-A1, PS-A2, PS-A3, PS-A4, PS-B1, PS-B2, PS-C1 scenario를 실행한다. PS-A4와 PS-B2에서는 필요한 subscriber/publisher process를 직접 시작하고 종료한다. |

실행은 아래 명령을 사용한다.

```bash
timeout 420s ./run_e2e.sh
```

`run_e2e.sh`는 Gradle `installDist`를 실행한 뒤 기본 registry, publisher, subscriber, client binary를
각각 띄운다. PS-A4 subscriber reconnect와 PS-B2 publisher restart의 lifecycle 제어는 Client support가
맡는다. 실패하면 `logs/<run-id>/` 아래 role별 stdout, stderr, message flow log를 출력한다.

현재 남은 gap은 push 기반 검증 경로다. scenario 동작은 모두 통과하지만, `.NET` PubSub와 마찬가지로
subscriber `/evidence` HTTP endpoint를 반복 조회해 marker를 확인한다. client stream connector를
사용하는 공통 push 검증 계약이 정리되면 이 gap을 별도 작업으로 닫는다.

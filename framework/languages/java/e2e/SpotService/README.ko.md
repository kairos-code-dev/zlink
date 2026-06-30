# Java SpotService E2E

이 디렉터리는 공통 E2E Config 2 SpotService 시나리오를 Java framework public API로 검증한다.
기존 단일 application 구현은 `.NET` 기준 역할 분리에 맞춰 Gradle subproject로 나누었다.

## 역할

- `Shared`: 기존 Java SpotService 구현의 공통 contract, spot, actor, handler, evidence, timer, stream support 타입.
- `Server/Registry`: embedded registry process.
- `Server/Play`: play node process. spot mesh, route mesh, ingress channel, stream endpoint를 호스팅한다.
- `Server/Publisher`: `ZLinkSpotPublisherClient` publish scenario process.
- `Client`: spot scenario driver process.

## 실행

```bash
./run_e2e.sh
```

runner는 registry, play, publisher, client role별 installDist binary를 직접 실행한다. 실행 로그와
evidence는 `logs/<run-id>/` 아래에 남는다.

완료/gap 분류는 `feature-map.ko.md`를 기준으로 본다. 공통 E2E나 다른 언어 구현만 근거로 Java public
API를 새로 추가하지 않는다.

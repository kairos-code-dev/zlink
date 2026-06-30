# Java RuntimeMonitoring E2E

이 디렉터리는 공통 E2E Config 7 Runtime Monitoring 시나리오를 Java framework public API로 검증한다.
기존 Java `Monitoring` 구현은 `.NET` 기준 이름과 역할 구조에 맞춰 `RuntimeMonitoring`으로 정렬했다.

## 역할

- `Shared`: 공통 request, reply, evidence 타입과 환경 변수 helper.
- `Server/Registry`: embedded registry와 registry monitoring source.
- `Server/Service`: channel, spot, socket/spot monitoring source, evidence endpoint.
- `Server/Trigger`: monitoring 등록 검증을 수행하는 trigger/validation process.
- `Client`: request와 malformed connection을 발생시키고 각 evidence endpoint를 검증하는 scenario client.

## 실행

```bash
./run_e2e.sh
```

runner는 role별 Gradle `installDist` binary를 사용한다. 로그와 evidence는 `logs/<run-id>/` 아래에 남는다.
현재 완료/gap 분류는 `feature-map.ko.md`를 기준으로 본다.

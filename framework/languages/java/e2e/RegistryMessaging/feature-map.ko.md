# Java RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Java framework E2E가 검증하는 항목을
정리한다. 실행 코드는 public Spring starter, `ZLinkClient`, `ZLinkRouteClient`, channel builder,
public HTTP client만 사용한다.

## 실행 구조 상태

- `implemented`: registry, provider, workflow, consumer, client는 별도 Gradle application과 별도
  process로 실행한다. 기존 단일 application의 role 환경 변수 분기는 제거했다.
- `implemented`: client scenario는 Java framework를 직접 호출하지 않고, 역할 server의 HTTP
  endpoint를 호출한다.
- `implemented`: provider, workflow, registry, consumer role은 HTTP health endpoint를 제공한다.
  provider와 workflow는 evidence 조회와 대기 endpoint도 제공한다.
- `implemented`: scenario ID별 client 파일과 공통 support 파일을 분리했다.
- `implemented`: `RM-A4`, `RM-B1`, `RM-B2`의 provider start/stop/replacement는
  `Client/Support/DynamicClusterLauncher.java`와 각 scenario 파일이 수행한다. runner는 해당
  scenario를 실행하고 결과를 수집한다.

## 구현됨

- `RM-A1`: registry discovery로 provider를 resolve하고 request를 보낸다. registry topology에서
  API channel의 ready router provider가 둘 이상 보이는지, provider evidence에 request가 남는지
  함께 검증한다.
- `RM-A2`: 수동 endpoint 연결로 provider에 직접 request를 보낸다.
- `RM-A4`: Client support가 같은 rid의 provider process를 새 endpoint로 교체한 뒤 follow-up
  request를 검증한다.
- `RM-A6`: 같은 registry 안에서 서로 다른 channel의 provider가 섞이지 않는지 검증한다. profile
  provider evidence와 workflow evidence를 각각 확인하고, workflow marker가 profile provider에
  기록되지 않았는지도 확인한다.
- `RM-B1`: Client support가 실행 중 provider를 추가하고 두 provider로 분산되는지 검증한다.
- `RM-B2`: Client support가 provider 하나를 정상 종료한 뒤 남은 provider로 request가 계속
  성공하는지 검증한다.
- `RM-C1`: request와 send happy path를 함께 검증한다. request와 command가 provider evidence에
  기록됐는지도 확인한다.
- `RM-C2`: route mesh에서 target rid request와 없는 rid 실패를 검증한다.
- `RM-C3`: 수동 multi-endpoint client/server channel에서 두 provider가 모두 처리하는지 검증한다.
- `RM-C4`: timeout 뒤 정상 request가 late reply에 오염되지 않는지 검증한다.
- `RM-C5`: 미등록 packet request 실패와 send drop 이후 정상 request 복구를 검증한다. message-flow
  observer가 남긴 `HANDLER_MISSING`/`REPLY_ERROR`, `HANDLER_MISSING`/`DROP` evidence도 확인한다.
- `RM-C7`: provider 시작 시 public runtime socket option으로 weight 75/25를 설정하고, manual
  multi-endpoint client 요청이 높은 weight provider 쪽으로 더 많이 분산되는지 검증한다.
- `RM-C8`: public typed client로 1 byte, 4 KiB, 256 KiB, 1 MiB payload 왕복을 검증한다. max size 초과
  거부는 framework channel runtime의 max message size 적용이 public typed channel 경로에 배선된
  뒤 같은 scenario의 추가 marker로 확장한다.

## Backpressure 범위

- `RM-C9`: Java는 느린 handler에 다량 one-way send를 동시에 제출하고, public bounded-failure
  oracle 없이 후속 request와 evidence가 회복되는지 검증한다. 직접적인 HWM 오류 결과 검증은
  binding/runtime 내부 테스트 범위로 둔다.

## 검증

- `../../gradlew --project-cache-dir /tmp/zlink-rm-gradle-cache --no-daemon compileJava`
  - 결과: `BUILD SUCCESSFUL`
- `timeout 420s ./run_e2e.sh`
  - 결과: common, weighted, scale-out, scale-in, failover 단계가 모두
    `registry-messaging e2e result=passed` 출력
  - 로그: `logs/20260702-060659-58435/`
- `timeout 420s framework/languages/java/e2e/RegistryMessaging/run_e2e.sh RM-C9`
  - 결과: `scenario RM-C9 passed`, `registry-messaging e2e result=passed`
  - 로그: `logs/20260701-035750-1917120`

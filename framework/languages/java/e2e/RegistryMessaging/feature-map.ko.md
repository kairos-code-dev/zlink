# Java RegistryMessaging E2E feature map

이 문서는 Config 1 Location Messaging 공통 시나리오 중 Java framework E2E가 검증하는 항목을
정리한다. 실행 코드는 public Spring starter, `ZLinkClient`, `ZLinkRouteClient`, channel builder,
public HTTP client만 사용한다.

## 실행 구조 상태

- `implemented`: provider, workflow, consumer, client는 별도 Gradle application과 별도 process로
  실행한다. runner는 실행별 전용 Redis container와 key prefix를 준비하고 모든 scenario가 같은
  Redis location store extension을 공유한다.
- `implemented`: client scenario는 Java framework를 직접 호출하지 않고, 역할 server의 HTTP
  endpoint를 호출한다.
- `implemented`: provider, workflow, consumer role은 HTTP health endpoint를 제공한다. provider와
  workflow는 evidence 조회와 대기 endpoint도 제공한다.
- `implemented`: scenario ID별 client 파일과 공통 support 파일을 분리했다.
- `implemented`: `RM-A4`, `RM-B1`, `RM-B2`의 provider start/stop/replacement support는 Redis
  location store 입력을 넘기고, consumer 재시작 없이 lifecycle 변화를 검증한다.

## 구현됨

- `RM-A1`: Redis location store 자동 연결로 provider를 resolve하고 request를 보낸다. consumer의
  public runtime query에서 API channel의 live provider peer row가 둘 이상 보이는지, provider
  evidence에 request가 남는지 함께 검증한다.
- `RM-A2`: 수동 endpoint 연결로 provider에 직접 request를 보낸다.
- `RM-A4`: 같은 rid의 provider를 새 endpoint로 교체한 뒤, consumer 재시작 없이 replacement
  provider로 request가 가는지 검증한다.
- `RM-A6`: API channel과 workflow channel이 같은 location store를 공유해도 channel 이름별로
  분리되는지 검증한다.
- `RM-B1`: provider 추가 뒤 consumer가 location store row를 보고 새 provider를 routing 대상에
  포함하는지 검증한다.
- `RM-B2`: provider 정상 종료 뒤 row 제거와 남은 provider routing을 검증한다.
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

- `../../gradlew --project-cache-dir /tmp/zlink-rm-gradle-cache --no-daemon compileJava --console=plain`
  - 결과: `BUILD SUCCESSFUL`
- `timeout 420s ./run_e2e.sh`
  - 결과: common, weighted, scale-out, scale-in, failover 단계가 모두
    `registry-messaging e2e result=passed` 출력
  - 로그: `logs/20260707-220606-3599616/`
  - runner가 전용 Redis location store를 준비하는 경로로 재검증했다. `RM-C9` recovery evidence는
    location store 연결이 선택할 수 있는 양쪽 provider를 합산해 확인한다.
- 단일 scenario 검증:
  - `RM-A1`: `logs/20260703-200744-25342/`
  - `RM-A2`: `logs/20260703-201929-65452/`
  - `RM-A4`: `logs/20260703-203441-25665/`
  - `RM-A6`: `logs/20260703-203947-47286/`
  - `RM-B1`: `logs/20260703-203700-34669/`
  - `RM-B2`: `logs/20260703-203720-36761/`
  - `RM-C1`: `logs/20260703-201837-59704/`
  - `RM-C2`: `logs/20260703-202210-80467/`
  - `RM-C3`: `logs/20260703-201954-68646/`
  - `RM-C4`: `logs/20260703-201126-38889/`
  - `RM-C5`: `logs/20260703-201906-63144/`
  - `RM-C7`: `logs/20260703-202238-83024/`
  - `RM-C8`: `logs/20260703-202115-75588/`
  - `RM-C9`: `logs/20260707-220422-3590936/`

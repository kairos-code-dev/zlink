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
- `partial`: `RM-A4`와 `RM-B2`는 public drain 결과가 terminal `Drained`인지 확인한 뒤 process를
  종료하고 row 제거를 기다린다. `RM-B2`의 drain 전파 중 지속 요청 완료는 아직 검증하지 않는다.

## 구현됨

- `RM-A1`: Redis location store 자동 연결로 provider를 resolve하고 request를 보낸다. consumer의
  public MeshNode runtime snapshot에서 API channel의 provider MeshNode descriptor가 둘 이상 보이는지, provider
  evidence에 request가 남는지 함께 검증한다.
- `RM-A2`: 수동 endpoint 연결로 provider에 직접 request를 보낸다.
- `RM-A4`: v1 terminal `Drained`와 old row 제거를 확인하고 같은 rid의 새 endpoint로 교체한다.
  runtime query에서 `api-a`의 유효 row가 정확히 하나이고 endpoint가 v2인지 확인한 뒤, 연속 20개
  request의 instance id가 모두 v2인지 검증한다.
- `RM-A6`: API channel과 workflow channel이 같은 location store를 공유해도 channel 이름별로
  분리되는지 검증한다.
- `RM-B1`: provider 추가 뒤 consumer가 location store row를 보고 새 provider를 routing 대상에
  포함하는지 검증한다.
- `RM-B2` (부분 구현): terminal `Drained`, row 제거와 남은 provider routing을 검증한다. drain 전파
  구간의 target 미지정 요청이 오류나 pending 없이 끝나는지는 아직 검증하지 않는다.
- `RM-C1`: request와 send happy path를 함께 검증한다. request와 command가 provider evidence에
  기록됐는지도 확인한다.
- `RM-C2` (차단): 존재하는 rid의 target request는 검증한다. 미존재 rid의 기대값을
  `REQUEST_TARGET_NOT_FOUND`로 바꾼 집중 gate는 실제 `TimeoutException`으로 실패했다. Java runtime이
  현재 RouteMesh member snapshot을 target 선택 전에 판정하지 못하므로 runtime 수정 뒤 닫는다.
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

- `RM-C9`(전환 필요): 현재 실행은 다량 one-way send 제출과 후속 request 회복을
  검증한다. non-blocking submit의 즉시 backpressure 결과와 blocking submit의 bounded
  admission 결과를 public send call에서 직접 대조해야 완료된다.

## 갱신된 계약의 남은 항목

- `RM-B3`은 미구현이다. Java catalog의 `failover` 별칭은 실제로 `RM-A4` 정상 replacement를 실행하며,
  provider `SIGKILL`, lease 만료 전후의 유한 결과, 남은 provider의 지속 성공을 검증하지 않는다.
- 위 `RM-B2`에 slow in-flight와 drain 전파 중 target 미지정 요청 20개를 넣은 집중 gate는 여러 요청이
  framework의 5초 request timeout으로 실패했다. draining peer가 신규 부하에서 제외되는 runtime
  수정 전까지 전파 구간 완료 조건은 차단 상태다.
- 동적 역할 readiness는 3초, peer convergence는 이름 있는 5초 route settle로 제한한다. 이 상한 안에
  준비되지 않는 역할이나 peer를 대기 시간 확대로 완료 처리하지 않는다.

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

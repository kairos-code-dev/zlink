# Java/Kotlin runtime·unit·SubmitAdmission 후속 검증 기록

실행 시각은 `2026-08-03 09:25 KST`이며, 작업 경로는
`/home/hep7/project/kairos/zlink`이다. 이번 기록은 runtime source fix,
regression unit, 승인 package process evidence를 분리한다.

## 발견한 runtime gap과 수정

SubmitAdmission에서 target process를 종료한 뒤 public RouteMesh snapshot이
peer를 `NOT_CONNECTED`로 바꾸었는데, Java raw Spot의
`classifyNodeSendTarget()`가 `CLOSED` peer를 단순히 known peer로 취급했다. 그
결과 one-way send가 `ROUTE_NOT_CONNECTED`로 즉시 끝나지 않고 admission deadline까지
재시도한 뒤 `DEADLINE_EXCEEDED`로 끝났다.

현재 구현은 peer state를 다음과 같이 분류한다.

- `ADMITTED`, `CONFIGURED`, `CONNECTING`: admission retry를 계속할 수 있는 상태
- `NOT_REQUIRED`: target authority가 없으므로 `REQUEST_TARGET_NOT_FOUND`
- `CLOSED`, `DRAINING`, `ERROR`: route가 끊겼으므로 `ROUTE_NOT_CONNECTED`

수정 경로는 다음과 같다.

- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaRawSpotNode.java`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaRawMeshNode.java`

production default liveness는 변경하지 않았다. raw mesh node에 package-private test
constructor를 추가해 unit에서만 짧은 probe/deadline을 주입할 수 있게 했다.

## Regression unit

추가한 test는
`ZLinkJavaRawMeshNodeM6ATest.closedExpectedPeerClassifiesDirectSendAsRouteNotConnected`이다.
두 raw MeshNode를 실제 Core socket으로 연결하고, peer를 닫은 뒤 `CLOSED` 상태를
관찰한 다음 direct target classification이 `ROUTE_NOT_CONNECTED`인지 확인한다.

실행 명령:

```text
./gradlew --no-daemon --no-parallel --max-workers=5 --rerun-tasks \
  :zlink-framework-core:test \
  :zlink-framework-kotlin:test \
  :zlink-framework-kotlin:contractTest \
  :zlink-stream-connector:test \
  :zlink-http-client:test \
  :zlink-http-client-kotlin:test
```

결과는 exit `0`, `BUILD SUCCESSFUL`, `37 actionable tasks: 37 executed`이다.
Java core는 `726 tests`를 실행했고, 새 closed-peer regression도 포함해
통과했다.

## 승인 package process evidence

다음 승인 입력을 그대로 사용했다.

- Core prefix: `.artifacts/wsl/install/zlink-core/11.1.0`
- Core evidence: `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/core-package-20260801.json`
- Java binding: `.artifacts/wsl/maven/systems/zlink/zlink/11.1.1/zlink-11.1.1.jar`
- binding SHA-256: `ca16f423b16c4b1d8cfc285dc0cf81165f46a773718372b3af8534dae6265849`
- embedded/approved Core runtime SHA-256: `b6fadc481c649b50637a9c0eb01d15a016e6ba4cd5bab967bdb6da4497a3c0c4`

실행 selector:

```text
SA-E2E-01,SA-E2E-05,SA-E2E-08,SA-E2E-09,SA-E2E-14,SA-E2E-20
```

결과는 exit `0`이며 process log는
[`SubmitAdmission process log`](../../../../languages/java/e2e/SubmitAdmission/logs/20260803-092352-472365/process.log)에
있다. aggregate evidence는
[`evidence.jsonl`](../../../../languages/java/e2e/SubmitAdmission/logs/20260803-092352-472365/evidence.jsonl)에서
확인한다.

- `SA-E2E-01`: Node direct와 ChannelName 모두 `Submitted`, target handler 2회
- `SA-E2E-05`: unknown RID 100회 `REQUEST_TARGET_NOT_FOUND`, 종료된 RID 100회 `ROUTE_NOT_CONNECTED`
- `SA-E2E-08`: local·remote 모두 `Submitted`, 각 handler 1회
- `SA-E2E-09`: positive-weight channel handler 1회
- `SA-E2E-14`: subscriber 없는 submit `Submitted`, late delivery 0회
- `SA-E2E-20`: handler completion 전에 submit terminal, gate 해제 뒤 handler 1회

이는 SubmitAdmission의 일부 process selector evidence다. Config 13의 20개
scenario, `SA-REG-01`, 전체 aggregate·sample·CI 완료를 의미하지 않는다.

## API snapshot과 packaged consumer 재검증

다음 명령을 같은 source tree에서 다시 실행했다.

```text
./scripts/verify_api_snapshot.sh java
./scripts/verify_api_snapshot.sh kotlin
./scripts/verify_packaged_contract.sh java
./scripts/verify_packaged_contract.sh kotlin
```

네 명령 모두 exit `0`이다. Java snapshot은 SHA-256
`dd5f7c3e071c7bf290d95148921919684ea84d46cd9a73c21db88e3374d6d994`, 2,851 lines이고,
Kotlin snapshot은 SHA-256
`0bf535f36206e713566375a40555a36d6fe1511dc905add8e89610cb75b4f287`, 3,359 lines이다.
이번 closed-peer fix는 exported public surface를 변경하지 않았고, Java·Kotlin clean
package consumer도 통과했다.

## 현재 판정

runtime source·unit matrix와 위 SubmitAdmission focused process는 통과했다. 남은
조건은 별도 E2E/sample/CI gate이며, JK-IMP-005의 routed transfer/restart/takeover와
JK-IMP-011의 HTTP process cleanup 같은 process-level evidence는 이 기록으로 닫지
않는다.

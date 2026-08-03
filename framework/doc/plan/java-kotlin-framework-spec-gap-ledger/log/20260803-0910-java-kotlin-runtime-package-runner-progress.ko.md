# Java/Kotlin runtime·package·SubmitAdmission runner 진행 기록

실행 시각은 `2026-08-03 09:10 KST`이며, 작업 경로는
`/home/hep7/project/kairos/zlink/framework/languages/java`이다. 이번 기록은
runtime unit 재실행, 승인된 Core·binding package preflight, API snapshot과 clean
consumer, SubmitAdmission runner의 현재 차단 조건을 구분한다.

## Runtime unit 재실행

다음 명령을 `--rerun-tasks`, `--max-workers=5`로 실행했다.

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
Java core test는 `726 tests`를 실행했다. stream HWM drop-newest와 error observer,
handler-less queue/waitFor ownership, content type, actor admission과 HWM lease,
Kotlin coroutine/HTTP deadline, Socket Poller·STREAM receive 회귀를 포함한다.

## 승인 package와 public surface

SubmitAdmission runner의 이전 `10.6.3` 고정값을 현재
`framework/languages/java/gradle/libs.versions.toml`의 `zlinkBindings=11.1.1`에서
읽도록 바꿨다. runner는 이제 `core/build`를 기준으로 삼지 않고 다음 승인 package를
검증한다.

- Core prefix: `.artifacts/wsl/install/zlink-core/11.1.0`
- Core evidence: `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/core-package-20260801.json`
- Java binding: `.artifacts/wsl/maven/systems/zlink/zlink/11.1.1/zlink-11.1.1.jar`
- binding SHA-256: `ca16f423b16c4b1d8cfc285dc0cf81165f46a773718372b3af8534dae6265849`
- embedded/approved Core runtime SHA-256: `b6fadc481c649b50637a9c0eb01d15a016e6ba4cd5bab967bdb6da4497a3c0c4`

`verify_api_snapshot.sh java`, `verify_api_snapshot.sh kotlin`,
`verify_packaged_contract.sh java`, `verify_packaged_contract.sh kotlin`은 모두
exit `0`이다. API snapshot hash는 Java
`dd5f7c3e071c7bf290d95148921919684ea84d46cd9a73c21db88e3374d6d994`, Kotlin
`0bf535f36206e713566375a40555a36d6fe1511dc905add8e89610cb75b4f287`이다.

## SubmitAdmission 결과

승인 package를 명시해 다음 selector를 실행했다.

```text
SA-REG-02,SA-REG-03
```

두 selector 모두 exit `0`이다. Gradle init script가 Java와 Kotlin의
`testRuntimeClasspath`에서 `systems.zlink:zlink:11.1.1`을 정확히 하나 resolve했고,
resolved jar SHA-256이 candidate와 같음을 출력했다. 이는 runtime unit과 package
candidate 경계를 확인하는 결과다.

`SA-REG-01`은 exit `1`이다. 실패 내용은 runtime test failure가 아니라 공통
submit-contract verifier가 문서 표현을 literal fragment로 요구하는 contract gate
문제다. 현재 보고된 항목은 `RuntimeShutdown` 2건, Config 13 semantic 3건,
`timeout 예외`, `target이 0개`, `partial`, `monitoring` coverage 4건이다.

process selector
`SA-E2E-01,05,08,09,14,20`은 승인 package preflight 뒤 role compile에서 exit `1`로
중단했다. 현재 E2E role source가 Framework public surface와 어긋나 다음 symbol을
찾지 못한다.

- `ZLinkRouteSendContext`, `ZLinkSendContext`
- `ZLinkMeshChannelBuilder.setWeight(int)`
- `ZLinkMeshPeerSnapshot.ready()`, `ZLinkMeshPeerSnapshot.rid()`

따라서 이번 실행에서는 client-visible admission 결과나 role server evidence를
생성하지 않았다. 이 차단은 Java/Kotlin runtime unit 완료 판정과 별도의 E2E/sample
contract drift이며, SubmitAdmission process 완료로 표시하지 않는다.

## 현재 판정

runtime 구현·unit·contract·API snapshot·clean package consumer 조건은 통과했다.
공통 374 scenario inventory, SubmitAdmission role process, 전체 E2E/sample/CI와
최종 POSD/DDD review는 여전히 blocker다. 다음 재개 지점은 현재 public interface에
맞춰 SubmitAdmission role source를 정렬하고, 같은 승인 package로 process evidence를
다시 생성하는 것이다.

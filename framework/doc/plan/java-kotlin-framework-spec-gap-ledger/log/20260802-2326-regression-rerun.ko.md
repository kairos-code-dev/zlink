# Java/Kotlin runtime regression 재실행 기록

2026-08-02 23:26 KST 기준이다. 직전 runtime checkpoint commit
`0740adfdac3622f87ad1d33965337b7c9552818f` 이후 동일한 Java/Kotlin source와 승인된
Core 11.1.0 package로 다시 실행했다.

| 검증 | 결과 |
|---|---|
| Java/Kotlin 전체 module test (`:zlink-framework-core:test`, `:zlink-framework-testkit:test`, `:zlink-framework-kotlin:test`, `:zlink-stream-connector:test`, `:zlink-http-client:test`, `:zlink-http-client-kotlin:test`) | PASS, 41 actionable tasks |
| Java/Kotlin `integrationTest` | PASS, 25 actionable tasks |
| `bindings/java :test` with Core 11.1.0 prefix | PASS, 5 actionable tasks |
| fresh binding package `systems.zlink:zlink:11.1.1` | PASS, embedded Core 11.1.0 |
| isolated actual Maven consumer | PASS, evidence `.artifacts/v11/evidence/V11-M4-BIND-JVM/java-binding-consumer-20260803-r4.json` |
| Java/Kotlin packaged contract consumer | PASS |
| `git diff --check` | PASS |

`JavaDocumentationRegressionTest`의 common 374 scenario inventory와 Java PubSub PS-B2는
runtime 변경과 독립된 기존 process/E2E blocker로 계속 분리한다. inventory 전체를
통과했다고 보고하지 않는다.

# Codex 문서 독립 리뷰 — S3 iteration 22

## 실행 증거

- provider: OpenAI
- reviewer: Codex agent
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- 시작·종료 파일별 hash: 205개 전부 일치
- `scope-files.sha256` SHA-256: `3e890ba18a9e9bab6a4b116128e1ba6bc1ce219da5e4ab1c1d3b08c5015ac9b4`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- 결과: finding이 있으므로 iteration 22 무효

## Finding

[1차소스][high] framework/doc/framework/spec/90-implementation-gap.ko.md:17,848; framework/doc/framework/spec/gaps/cpp.ko.md:211,341 — 중앙 소유권 설명은 전 언어 공통 gap을 여전히 §12.20~§12.24로 제한하고 다섯 언어 문서는 해소된 §12.21보다 §12.24를 먼저 고쳐야 한다는 과거 선행 관계를 현재 작업 규칙으로 유지한다 — §12.27~§12.34도 중앙이 소유해야 하며 현재 source는 모든 언어에서 §12.21을 이미 해소했지만 §12.24는 열려 있어 문장 자체가 현재 상태와 모순된다 — 공통 gap 범위를 §12.20 이후로 넓히고 남은 선행 관계를 §12.24와 sample 사용처로 다시 쓴다.

[1차소스][high] framework/doc/framework/spec/90-implementation-gap.ko.md:735-750 — 중앙 IMP-X 표가 언어별 최신 체크리스트와 어긋난다 — startup validation, compressed payload limit, proxy credential, timer validation, correlation fallback, paging, store failure grace, cancellation과 Redis fixture는 언어별 완료 증거가 있지만 중앙은 계속 미해결로 분류하고, 반대로 실제 열린 언어 집합도 넓게 적는다 — 언어별 체크 상태와 source·contract test를 다시 대조해 미해결 언어를 X2(Java/C++), X5·X6(Java/Node/C++), X8·X12·X16(Java), X14(C++)로 줄이고 나머지는 해소로 기록한다.

[1차소스][medium] framework/doc/framework/spec/gaps/java.ko.md:304 — 중앙 IMP-X4는 감사 근거 오류로 해소했지만 Java 공통 결함 체크리스트에는 여전히 미해결로 남아 있다 — 정식 spec에 store read별 5초 상한이 없고 같은 문서의 IMP-JV-10은 이미 이를 해소로 표시한다 — IMP-X4도 해소로 맞추고 완료 수치를 갱신한다.

[1차소스][medium] framework/doc/framework/spec/server/languages/dotnet/02-handler-interfaces.ko.md:579; framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:352; framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md:715 — ActorRef exact interface가 Core 정식 계약과 언어 사이에서 일치하지 않는다 — Core `zlink_actor_ref_t`는 Node RID, Actor ID, generation 세 값이고 .NET 실제 binding도 같은 readonly struct지만 exact 문서는 sealed class로 적고 NodeRid를 누락한다. Java source는 같은 세 값의 record인데 exact 문서에 선언이 없고, C++ exact·source만 actor type을 추가로 노출한다. ActorRefSnapshot도 .NET·Java exact에서 빠져 있다 — 공통 Framework spec에 Core와 같은 세 값을 고정하고 .NET·Java exact 선언과 snapshot을 완결하며 C++의 추가 actor type은 구현 gap으로 추적한다.

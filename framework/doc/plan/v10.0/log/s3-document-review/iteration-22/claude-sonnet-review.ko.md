# Claude Sonnet 문서 독립 리뷰 — S3 iteration 22

## 실행 증거

- provider: Anthropic Claude CLI
- model: Claude Sonnet 5 (`claude-sonnet-5`)
- session ID: `79013174-3ee0-43a4-9555-4d4c9058a523`
- terminal UUID: `61043af0-541b-40ea-80af-43404fc5332f`
- 도구: read-only `Read`, `Grep`, `Glob`, `Bash`
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- 시작·종료 파일별 hash: 205개 전부 일치
- `scope-files.sha256` SHA-256: `3e890ba18a9e9bab6a4b116128e1ba6bc1ce219da5e4ab1c1d3b08c5015ac9b4`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- 결과: finding이 있으므로 iteration 22 무효

## Finding

[1차소스][high] framework/doc/framework/dotnet/guide/09-stream.ko.md:348 — .NET STREAM guide가 송신 한도를 압축 전 원본 크기로 검사한다고 안내한다 — 공통 Connector §4.7과 해소된 IMP-DN-13, 현재 구현은 압축을 사용하면 압축된 wire payload를 기준으로 검사한다 — 현재 동작으로 고친다.

[1차소스][medium] framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:352 — Java exact catalog가 `ActorRef`를 반복 사용하면서 record 선언을 제공하지 않는다 — Java public source는 `ActorRef(RoutingId nodeRid, String actorId, long generation)` record를 제공하고 다른 exact catalog는 해당 타입을 선언한다 — Java exact catalog에 정식 선언을 추가한다.

[1차소스][medium] framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md:715; framework/doc/framework/spec/server/languages/dotnet/02-handler-interfaces.ko.md:579 — C++ ActorRef는 Node RID·Actor type·Actor ID·generation을 노출하지만 .NET 문서는 Actor ID·generation만 노출해 같은 공통 개념의 필드 집합이 다르다 — Core 정식 ActorRef와 공통 Framework spec을 기준으로 다섯 언어 필드 집합을 정렬하고 구현 차이는 gap으로 기록한다.

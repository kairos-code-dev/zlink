# S3 문서 독립 리뷰 — iteration 24 (Claude Sonnet)

## 실행 증거

- provider/model: Anthropic / Claude Sonnet 5 (`claude-sonnet-5`)
- session ID: `3deb4006-6e3d-42cc-8b83-2312f48ad607`
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- `scope-files.sha256` SHA-256: `517016d8ad95ca1c334b2ec747e3219864a4a5ee99ef5c971a32da4aec0ae4a9`
- 파일별 hash: 시작·종료 205/205 일치
- 첫 출력은 guide와 큰 exact-interface 문서 약 23개의 전수 읽기를 완료하지 않았다고 밝혀 채택하지 않았다.
  같은 session을 재개해 누락 문서를 직접 완독하고 종료 hash를 다시 검증한 최종 결과만 아래에 기록한다.
- read-only 실행, 작성 파일 0개, 기존 dirty worktree 보존

## Findings

[1차소스][major] framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md:1751 — Node `ZLinkStreamSessionError`에 `Internal` 값이 없다 — .NET·Java·C++ exact interface는 모두 internal과 transport error 두 값을 선언하지만 Node만 `TransportError` 하나이며 Node gap에도 이 누락이 없다 — Node 선언에 `Internal = "internal"`을 추가하거나 의도된 언어별 축소라면 gap에 사유와 계획을 기록한다.

[1차소스][minor] framework/doc/framework/cpp/guide/07-channel-messaging.ko.md:24 — C++ guide의 다른 언어 대비 설명이 attribute 기반 자동 등록을 제공하는 언어로 .NET·Node·Java만 나열하고 Kotlin을 빠뜨렸다 — Kotlin guide도 annotation과 package scan 기반 자동 등록을 기본으로 명시한다 — 목록에 Kotlin을 포함한다.

[원칙][minor] framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:1901 — 섹션 번호가 §8.1 다음 바로 §15.1로 건너뛰어 §9~§14가 존재하지 않는다 — 불연속 번호는 독자가 문서 구조를 탐색하고 참조하기 어렵게 만든다 — 번호를 문서 구조에 맞게 순차 재정렬한다.

[원칙][minor] framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md:2141 — 섹션 번호 `2.24`가 서로 다른 두 절에 중복 사용됐다 — 앞선 `2.24`와 목표 계약 적용 추적 절이 같은 번호를 사용한다 — 후자를 실제 다음 순번으로 교정한다.

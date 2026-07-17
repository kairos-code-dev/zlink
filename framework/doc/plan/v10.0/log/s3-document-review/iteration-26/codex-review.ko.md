# S3 문서 독립 리뷰 — iteration 26 (Codex)

## 실행 증거

- provider/model: OpenAI / GPT-5.6 (`gpt-5.6-sol`, Codex CLI 기본 모델)
- session ID: `019f6ece-f381-7a12-9a3b-0b2e47819d12`
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- `scope-files.sha256` SHA-256: `f7d6e6c8b1d6ab2db6eb9a3001d503a988108b7079e3cfcb6bfb42853a0045d7`
- 파일별 hash: 시작·종료 205/205 일치
- 205개 파일 전체 검토, contract verifier와 실제 pymdownx local link 검사 오류 0
- read-only 실행, 작성 파일 0개, 기존 dirty worktree 보존

## Findings

[원칙][high] framework/doc/framework/common/e2e/config-8-execution-turn.ko.md:279 — Entry Spot에서 user Spot으로 이동하는 join만 source leave가 없다고 규정한다 — server/23의 join commit 순서와 config 10 및 .NET guide는 Entry Spot도 source이므로 target `OnActorJoin`, location CAS commit, source `OnLeaveActor`, target `OnJoinedActor` 순서를 요구한다 — TD-E1도 같은 lifecycle 순서로 고친다.

[원칙][medium] framework/doc/framework/spec/stream-connector/languages/java/03-stream-connector.ko.md:110 — Java 문서는 `MANUAL` mode의 `waitFor` 완료에 `dispatch().submit()`이 필요하다고 규정한다 — C++ exact interface는 두 mode 모두 unread 수신 큐를 직접 소비한다고 규정해 언어 간 의미가 다르다 — 공통 문서가 `waitFor` 계열과 callback dispatch의 경계를 소유하게 하고 Java 문서를 일치시킨다.

[원칙][medium] framework/doc/framework/spec/stream-connector/32-stream-connector.ko.md:504 — 공통 문서는 세 대기 표면의 인자가 모두 `name` 하나라고 고정한다 — C++와 Java의 exact interface에는 payload type에서 packet 이름을 결정하는 overload가 있어 공통 설명과 충돌한다 — packet 이름을 명시하거나 payload type에서 결정한다는 의미만 공통 계약으로 두고 정확한 인자와 overload는 언어별 문서가 소유하게 한다.

[1차소스][low] framework/doc/framework/spec/http-client/12-http-client.ko.md:142 — codec registry 분리 근거를 server/11 §6으로 연결한다 — 실제 소유 문서는 server/30 §5이며 현재 링크에서는 근거를 확인할 수 없다 — server/30 §5의 정확한 anchor로 바꾼다.

[1차소스][low] framework/doc/framework/spec/server/41-location-store-redis.ko.md:194 — Redis 장애 변환 근거를 주소 없는 `계약 §3.1`로 표시한다 — 관련 fail-static과 recovery 계약은 server/40 §7이 소유하므로 현재 참조는 검증할 수 없다 — server/40 §7을 직접 연결한다.

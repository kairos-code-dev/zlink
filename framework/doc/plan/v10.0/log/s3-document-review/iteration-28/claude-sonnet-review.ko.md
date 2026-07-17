# Claude Sonnet 독립 문서 리뷰 — S3 iteration 28

[1차소스][high] `framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md:727` — `actor_join_accepted_t`와 `actor_join_rejected_t` 두 variant alternative가 동일한 이름의 `reply` 멤버를 노출해 accepted/rejected 분기 없이 접근할 수 있다 — `90-implementation-gap.ko.md` §15.5는 이 모양이 실제 버그를 유발했다고 기록하고 C++ rejected 멤버의 이름을 바꾸기로 결정했지만 정식 공개 계약에는 반영되지 않았다 — rejected의 멤버 이름을 바꾸어 분기 없는 공통 접근을 막는다.

[1차소스][high] `framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:459` — sealed interface `ZLinkActorJoinResult<TReply>`에 `reply()`가 공통 상위 메서드로 선언되어 승인·거절 분기 없이 호출할 수 있다 — `90-implementation-gap.ko.md` §15.5는 Java에서 이 호출이 rejected NPE를 만든 사례와 sealed interface에서 `reply()`를 제거한다는 결정을 기록하지만 정식 공개 계약에는 반영되지 않았다 — 공통 `reply()`를 제거하고 Accepted/Rejected record에서만 각 결과를 노출한다.

[1차소스][high] `framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md:108` — `ZLinkActorJoinResult<TReply>` union의 accepted/rejected 양쪽이 `reply: TReply`를 가져 TypeScript가 status 분기 없는 공통 속성 접근을 허용한다 — `90-implementation-gap.ko.md` §15.5는 거절 갈래가 reply 대신 거절 사유를 갖도록 결정했지만 정식 공개 계약에는 반영되지 않았다 — rejected 필드를 `reply`와 다른 거절 사유 이름으로 바꾼다.

[1차소스][low] `framework/doc/framework/spec/stream-connector/languages/typescript/03-stream-connector.ko.md:203` — `ZlinkStreamErrorCode`의 대부분은 camelCase 문자열을 쓰지만 `observer-failed`, `observer-dropped`, `received-message-dropped` 세 값만 kebab-case를 사용한다 — 같은 파일의 다른 string enum과 대응 언어 enum은 한 표기를 일관되게 사용한다 — 세 값을 `observerFailed`, `observerDropped`, `receivedMessageDropped`로 통일한다.

## 실행 증거

- provider/model/session: Anthropic / `claude-sonnet-5` / `d4e4629d-b229-4353-861f-d03a65d2aa25`
- scope-files SHA-256 205개가 시작과 종료 시 모두 일치했고 저장소 안팎에 파일을 만들거나 수정하지 않았다.
- `scripts/verify-framework-doc-contracts.sh`는 시작과 종료 모두 `FRAMEWORK DOC CONTRACTS CLEAN`이었다.
- 기준 commit은 `169c458ed238228d7a23cea089c8c467c96b953c`이며 review 중 drift가 없었다.

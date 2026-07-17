# S3 iteration 14 — Codex 독립 문서 리뷰

[원칙][blocker] framework/doc/plan/v10.0/log/s3-document-review/iteration-14/manifest.ko.md:22 — 종료 hash가 동결값과 다르므로 리뷰 결과를 채택할 수 없다 — 시작 시 202개 파일·목록 hash `dfabc81f…642f`·aggregate `59515134…1dd3`가 일치했으나, 종료 시 18개 파일이 변경되어 aggregate가 `c92693ee…a097`로 바뀌었다 — 새 revision을 동결하고 전체 리뷰를 다시 실행해야 한다.

시작 snapshot에서 확인한 참고 finding은 다음과 같다.

[1차소스][high] framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md:31 — public actor location을 `OnJoinedActor` 완료 뒤에만 관찰한다고 했지만 같은 문서 244~252행은 callback 지연 중 committed row 관찰을 요구한다 — server spec 23의 35~38행과 70~75행도 location commit과 activation을 구분한다 — durable location row와 ready route의 공개 시점을 분리해 서술해야 한다.

[1차소스][high] framework/doc/framework/common/e2e/config-11-observability-ops.ko.md:150 — transfer duration을 target activation과 success reply 확정까지로 정의한다 — metrics owner 문서 51의 87행은 activation 또는 실패 terminal까지로 고정한다 — 검증 구간을 owner 계약과 일치시켜야 한다.

[1차소스][high] framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md:1587 — 두 C++ 예제가 `request(mesh, channel, request)` 선언의 MeshName 인자를 누락한다; 같은 문제는 1994행에도 있다 — 선언은 1116~1119행에서 인자 세 개를 요구한다 — 두 예제에 MeshName을 추가해야 한다.

[원칙][high] framework/doc/framework/spec/http-client/09-error-model.ko.md:15 — C++ timeout의 공개 표현으로 내부 `detail::boundary_error_t::timed_out`를 노출한다; 28행도 같다 — 공개 header는 `framework_exception_t::code()`를 제공하고 내부 enum은 `detail` namespace에 둔다 — `code() == std::errc::timed_out` 같은 공개 표현으로 바꿔야 한다.

[1차소스][high] framework/doc/framework/spec/gaps/cpp.ko.md:874 — ST-A1의 계약 순서를 폐기된 `admission → leave → joined → location_committed`로 인용한다 — config 10의 88행과 server spec 23의 35~38행은 `location_committed → leave → joined` 순서다 — gap 판정과 구현 증거를 새 순서로 다시 평가해야 한다.

[1차소스][medium] framework/doc/framework/dotnet/guide/05-channel-messaging.ko.md:876 — dispatch reason/action을 `HandlerMissing`·`ReplyError`·`Drop`으로 안내한다 — message-flow owner 문서 52의 88~95행은 `no_handler`·`reply_error`·`drop`을 닫힌 값으로 고정한다; 같은 stale 값이 C++ PubSub feature-map 24행, SpotService feature-map 36행, C++ gap 885행과 Node gap 753행에도 남아 있다 — guide·feature-map·gap을 정식 닫힌 값으로 통일해야 한다.

[원칙][low] framework/doc/framework/cpp/guide/16-grpc-alternative.ko.md:91 — 원칙 문서가 금지 예시로 든 “빠르고 좋다”가 그대로 남아 있다 — Java/Kotlin STREAM guide의 “콜백은 직렬로 돈다” 등도 객체를 구어체로 표현한다 — 측정 가능한 특성과 중립적 기술 표현으로 고쳐야 한다.

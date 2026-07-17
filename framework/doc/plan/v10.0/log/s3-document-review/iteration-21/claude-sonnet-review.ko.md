# Claude Sonnet 문서 독립 리뷰 — S3 iteration 21

## 실행 증거

- provider: Anthropic Claude CLI
- model: Claude Sonnet 5 (`claude-sonnet-5`)
- session ID: `46f4e6f4-7daf-4322-992f-404bf6d68966`
- terminal UUID: `e68d05e4-7f37-4e24-9372-74f70fbd19fb`
- 도구: read-only `Read`, `Grep`, `Glob`, `Bash`
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- 시작·종료 파일별 hash: 205개 전부 일치
- `scope-files.sha256` SHA-256: `a66bb7c5066a5ae20d05123072eb9515491f22bbb82ea593816032b592234e14`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- 결과: finding이 있으므로 iteration 21 무효

## Finding

[1차소스][high] framework/doc/framework/spec/gaps/cpp.ko.md:311 — C++ §12.21은 미해결로, §12.23은 CPU/I/O worker 분리와 `yield`가 전부 없는 것으로 기록됐지만 source에는 `async()`와 `yield()`의 turn 유지·반납 구분과 두 worker가 구현돼 있다 — `call.hpp`, `worker.hpp`, `task.hpp`, `spots/spot.hpp`를 대조하면 §12.21은 해소됐고 §12.23의 남은 차이는 callback `std::stop_token` 전달이다 — 현재 구현 상태에 맞춰 두 gap을 정정한다.

[1차소스][medium] framework/doc/framework/spec/90-implementation-gap.ko.md:860 — 중앙 인덱스는 .NET 62건 전부 완료라고 적지만 언어 gap 문서는 현재 68건, 완료 61건, 미완료 7건이라고 적는다 — 같은 frozen scope의 수치가 서로 모순된다 — 중앙 표를 언어 문서의 현재 고유 gap 집계와 일치시킨다.

[1차소스][medium] framework/doc/framework/spec/90-implementation-gap.ko.md:755 — `[54 §3.4]`와 `[40 §6.1]`은 대상 문서에 존재하지 않는 하위 절이다 — store read 상한과 `storeFailureGrace` 근거를 실제 소유 절로 고치고 같은 잘못된 인용을 언어 gap 문서에서도 정정한다.

[원칙][low] framework/doc/framework/java/guide/04-channel-messaging.ko.md:41 — Java guide의 상호 참조 표시 번호가 실제 파일명보다 하나씩 크다 — 링크 대상은 맞지만 표시 이름 `06-spot`, `07-actor-session`, `08-stream`을 실제 `05`, `06`, `07`로 고친다.

[1차소스][medium] framework/doc/framework/kotlin/guide/06-actor-session.ko.md:230 — 예제가 존재하지 않는 `tryHandleAsync(context, header, payload)`와 스코프에 없는 `header`를 사용한다 — 실제 Java interface는 `tryHandle(context, dispatch, payload)`가 `CompletionStage<Boolean>`을 반환한다 — Kotlin 예제를 `handlers.tryHandle(context, dispatch, payload).await()`로 고친다.

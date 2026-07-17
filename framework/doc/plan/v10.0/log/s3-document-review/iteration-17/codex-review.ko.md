# S3 iteration 17 — Codex 독립 문서 리뷰

[원칙][blocker] iteration 17의 리뷰 도중 scope hash가 바뀌어 결과를 확정할 수 없다 — 시작 시
202개 파일과 manifest aggregate `dd8d4039…9371`이 모두 일치했지만, 리뷰 시작 뒤 아래 4개 파일이
수정되어 중간 aggregate가 `8c0bfa04…c5d4`로 바뀌었다 — scope를 수정하는 별도 작업이 끝난 뒤 새
revision을 동결하고 전체 리뷰를 다시 실행한다.

Drift 파일은 다음과 같다.

- `framework/doc/framework/spec/90-implementation-gap.ko.md`
- `framework/doc/framework/spec/gaps/cpp.ko.md`
- `framework/doc/framework/spec/gaps/dotnet.ko.md`
- `framework/doc/framework/spec/gaps/java.ko.md`

리뷰 agent는 coordinator가 drift를 확인한 즉시 중단했다. 파일은 수정하지 않았으며 finding 검토 결과는
채택하지 않는다.

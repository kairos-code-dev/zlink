# Iteration 10 Codex review

## 판정

`DOC REVIEW NOT CLEAN`

## Finding

- `[원칙][medium] framework/doc/framework/dotnet/guide/06-spot.ko.md:14` — 10.0.0 목표 API를 현재 구현
  기준으로 표시한다. 목표 사용법으로 바꾸고 현재 source 차이는 gap 문서만 소유해야 한다.
- `[원칙][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:15` — 아직 컴파일되지 않는
  목표 예제가 있는데 모두 contract test에서 실행된 코드라고 단언한다. 구현 전 목표 계약 예문으로
  표시해야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:118` — handler filter가
  잘못된 반환형, delegate와 인자를 사용하고 publish에도 적용된다고 설명한다. Exact filter signature와
  ChannelName send/request 범위로 바꿔야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:210` — 존재하지 않는
  metadata builder와 runtime policy를 공개한다. 방향별 allowlist builder만 사용해야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:406` — one-way call을
  존재하지 않는 `Submit(ct)`로 끝낸다. `TrySubmit()` 또는 `SubmitAsync(ct)`를 사용해야 한다.
- `[계약][high] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:589` — STREAM fluent chain이
  컴파일되지 않고 `Submit()`과 reply metadata를 사용한다. Send에만 metadata를 적용하고 정확한 terminal
  call을 사용해야 한다.
- `[계약][medium] framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md:386` — fixed drain
  reason을 PascalCase로 적었다. 공통 snake_case 닫힌 값과 맞춰야 한다.
- `[원칙][medium] scripts/verify-framework-doc-contracts.sh:1932` — guide gate가 filter, metadata, submit과
  drain reason drift를 놓친다. Exact signature 기반 검사를 추가해야 한다.

시작·종료 hash와 96개 파일 hash는 일치했고 verifier와 `git diff --check`가 통과했다.

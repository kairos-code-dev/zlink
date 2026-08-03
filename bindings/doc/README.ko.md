# ZLink Bindings 문서

`ZLink` 언어 바인딩 문서다. core 라이브러리 문서는
[`core/doc/`](../../core/doc/README.ko.md), framework 문서는
[`framework/doc/`](../../framework/doc/README.ko.md)에 있다.

| 영역 | 위치 | 내용 |
|------|------|------|
| 사용자 가이드 | [guide/](guide/README.ko.md) | 언어별 binding 사용 가이드(cpp · dotnet · go · java · node · python · rust) |
| 스펙 | [spec/](spec/README.ko.md) | 언어별 binding 공개 계약(c · cpp · dotnet · go · java · node · python · rust) |
| 구현 전 초안 | [spec/draft/](spec/draft/README.ko.md) | 아직 공개 계약이 아닌 bindings 설계 후보 |
| 계획 | [공통 계획](plan/python-go-rust-core-11-update.ko.md) | Python · Go · Rust binding이 함께 사용할 Core 입력과 완료 기준 |

언어별 최신화는 [Python](plan/python-core-11-update.ko.md), [Go](plan/go-core-11-update.ko.md),
[Rust](plan/rust-core-11-update.ko.md) 실행 계획에서 각각 관리한다. Go와 Rust가 같은 반환 의미를 제공하는지는
[Go·Rust parity inventory](plan/go-rust-return-parity.ko.md)에서 검증한다.

# Bindings 공개 계약 초안 목록

> 이 디렉터리의 문서는 구현 전 초안이며 현재 공개 계약이 아니다. 구현과 contract test가 통과한 뒤
> `bindings/doc/spec/`의 대응 정식 문서에 내용을 나누어 반영한다.

## Core 11 bindings 최신화

- [Go·Rust submit 반환 계약](go-rust-submit-return.ko.md): backpressure, 성공 값 없는 terminal method와 Go
  `context.Context`의 목표 계약을 검토한다.
- [Python·Rust 단일 part 접근 이름](python-rust-single-part-naming.ko.md): 두 언어의 public method 이름과
  ownership을 검토한다.

## 이전 Core service 설계

- [RouteMesh Python·Go·Rust](route-mesh-python-go-rust.ko.md): 이전 Core service header를 기준으로 작성된
  초안이다. Core 11 raw-only 책임 경계의 구현 입력으로 사용하지 않으며 공통 실행 계획 PGR-COMMON-05에 따라
  필요한 Framework 요구를 분리한 뒤 삭제한다.

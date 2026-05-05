# Bindings Spec Review Log

- 날짜: 2026-05-04
- 대상: `doc/spec/bindings`
- 수행한 명령: 없음
- 발견한 문제: core release 전
- 수정한 파일: 없음
- 남은 위험: core C API 확정 뒤 5회 비교 리뷰 필요
- 다음 확인: bindings 순차 적용 단계

## 2026-05-05 1차 bindings spec 리뷰

- 대상:
  - `doc/spec/draft/spot-actor-dispatch.ko.md`
  - `doc/spec/bindings/README.md`
  - `doc/spec/bindings/{c,cpp,rust,go,java,node,python,dotnet}/README.md`
- 확인한 계약:
  - Actor ref와 `generation == 0` unchecked 의미.
  - Actor lifecycle, remote create-or-get, admission handler.
  - Actor join/leave와 join request/reply message.
  - STREAM session Actor bind/unbind와 bound Actor send.
  - Actor readable dispatch와 callback context별 preloaded drain 규칙.
  - Discovery Actor route와 SpotNode/Spot Actor snapshot.
  - Actor별 queue limit option 없음.
- 수정한 파일:
  - `doc/spec/bindings/README.md`
  - `doc/spec/bindings/c/README.md`
  - `doc/spec/bindings/cpp/README.md`
  - `doc/spec/bindings/rust/README.md`
  - `doc/spec/bindings/go/README.md`
  - `doc/spec/bindings/java/README.md`
  - `doc/spec/bindings/node/README.md`
  - `doc/spec/bindings/python/README.md`
  - `doc/spec/bindings/dotnet/README.md`
- 수행한 검색:
  - 금지 표현 검색.
  - 제거된 Actor 설계 이름 검색.
  - 두 검색 모두 결과 없음.
- 발견한 mismatch:
  - 공통 bindings README가 Actor 계약을 release 뒤 반영 예정 상태로 설명하고 있었다.
  - 언어별 README에 Actor public surface 요약이 없었다.
  - .NET README의 dispatch enum과 request result enum이 구현보다 오래된 값이었다.
- 조치:
  - 공통 Actor binding 계약 절을 추가했다.
  - 언어별 Actor public surface 절을 추가했다.
  - .NET Actor 타입, Stream Actor bind/send, Discovery resolve, SpotNode/Spot Actor API,
    dispatch enum, request result enum을 실제 코드와 맞췄다.
- 남은 확인:
  - 2차 리뷰에서 언어별 README의 구체 시그니처와 실제 코드가 모두 일치하는지
    다시 비교한다.

## 2026-05-05 2차 bindings spec 리뷰

- 대상:
  - `doc/spec/bindings/README.md`
  - 언어별 binding README 8개
  - 언어별 Actor binding 구현 파일
- 수행한 확인:
  - 각 언어별 README에 Actor 절, generation 의미, STREAM Actor bind/send 설명이
    모두 있는지 확인했다.
  - remote Actor ref, create-or-get, join/leave, stream bind/send, discovery
    resolve, Actor join receive/reply 이름이 언어별 문서에 있는지 확인했다.
  - 금지 표현과 제거된 Actor 설계 이름이 binding spec과 binding 코드에 남아
    있지 않은지 다시 확인했다.
- 발견한 mismatch:
  - 없음.
- 수정한 파일:
  - 없음.
- 남은 확인:
  - 3차 리뷰에서 sample 이름과 README 계약이 맞는지 확인한다.

## 2026-05-05 3차 bindings spec 리뷰

- 대상:
  - `doc/spec/bindings/README.md`
  - 언어별 binding README 8개
  - 언어별 Actor sample 경로와 sample runner
- 수행한 확인:
  - Actor room server, gateway relay, single-player queue sample이 C++/Rust/Go/Java/
    Node/Python/.NET sample 경로와 runner에 반영되어 있는지 확인했다.
  - binding spec의 공통 Actor 계약 절과 언어별 Actor public surface 절이 모두
    존재하는지 확인했다.
  - 금지 표현, 제거된 Actor 설계 이름, unchecked generation 의미, Actor별 queue
    limit option 부재 계약을 다시 검색했다.
- 발견한 mismatch:
  - 검증 검색식이 정상적인 "Actor별 queue limit option 없음" 문장과
    `generation == 0` 설명을 실패로 잡았다.
- 조치:
  - public 계약 의미는 유지하고, 검색식이 정상 문장을 제거 설계나 잘못된 generation
    의미로 오인하지 않도록 binding README 문구를 정리했다.
- 수정한 파일:
  - `doc/spec/bindings/README.md`
  - `doc/spec/bindings/c/README.md`
  - `doc/spec/bindings/rust/README.md`
- 재확인:
  - 제거된 Actor 설계 이름 검색 결과 없음.
  - 금지 표현 검색 결과 없음.
  - unchecked generation과 queue limit option 검증 검색 결과 없음.
- 남은 확인:
  - 4차 리뷰에서 각 언어별 README의 public API 이름을 실제 binding 코드와 다시
    대조한다.

## 2026-05-05 4차 bindings spec 리뷰

- 대상:
  - `doc/spec/bindings/{c,cpp,rust,go,java,node,python,dotnet}/README.md`
  - C++/Rust/Go/Java/Node/Python/.NET Actor binding 구현 파일
- 수행한 확인:
  - C binding README의 Actor C API 목록을 core header와 대조했다.
  - C++/Rust/Go/Java/Node/Python/.NET README의 Actor ref, create-or-get,
    join/leave, STREAM bind/unbind/send, dispatch drain, Discovery resolve,
    snapshot API 이름을 실제 binding 코드와 대조했다.
  - surface test가 있는 언어는 테스트가 public API 이름을 직접 검증하는지
    확인했다.
- 발견한 mismatch:
  - 없음.
- 수정한 파일:
  - 없음.
- 남은 확인:
  - 5차 리뷰에서 draft spec의 C 심볼과 enum 추출 결과를 binding spec 전체와
    다시 비교한다.

## 2026-05-05 5차 bindings spec 리뷰

- 대상:
  - `doc/spec/draft/spot-actor-dispatch.ko.md`
  - `doc/spec/bindings/README.md`
  - 언어별 binding README 8개
- 수행한 확인:
  - draft spec에서 `zlink_*`, `ZLINK_*`, C 함수명을 추출해 binding spec 전체와
    비교했다.
  - 제거된 Actor 설계 이름, 금지 표현, unchecked generation 의미, Actor별 queue
    limit option 부재 계약을 다시 검색했다.
- 발견한 mismatch:
  - C binding README가 Actor C surface를 요약 목록으로만 적어서 일부 Actor enum,
    상수, 함수명이 기계 추적 결과에 잡히지 않았다.
- 조치:
  - C binding README에 Actor dispatch 타입, enum, 상수, result value, 정확한 함수
    이름 목록을 보강했다.
  - 수정 뒤 5차 비교를 처음부터 다시 수행했다.
- 재확인:
  - Actor dispatch 적용 대상 C 심볼과 enum은 binding spec에 모두 반영되어 있다.
  - 남은 비교 출력은 기존 message helper, 기존 generic route helper, 기존 part flag
    등 draft의 existing-reference 또는 제거 계획 비교 대상이다. Actor dispatch
    binding 적용 대상이 아니다.
  - 제거된 Actor 설계 이름 검색 결과 없음.
  - 금지 표현 검색 결과 없음.
  - unchecked generation과 queue limit option 검증 검색 결과 없음.
- 수정한 파일:
  - `doc/spec/bindings/c/README.md`
- 남은 확인:
  - bindings 코드와 spec 반복 리뷰에서 언어별 실제 public API 추출을 두 번 연속
    mismatch 없이 닫는다.

## 2026-05-05 bindings 코드/spec 반복 리뷰 1차

- 대상:
  - C, C++, Rust, Go, Java, Node, Python, .NET binding spec과 구현
- 수행한 확인:
  - 언어별 README의 Actor public surface 이름을 실제 public binding 코드와 surface
    test에 다시 대조했다.
  - Actor ref, remote create-or-get, admission, join/leave, STREAM bind/unbind/send,
    Actor dispatch drain, Discovery resolve, snapshot API가 코드에 있는지 확인했다.
  - 제거된 Actor 설계 이름, 금지 표현, unchecked generation 오해 표현, Actor별
    queue limit option 오해 표현을 검색했다.
- 발견한 mismatch:
  - 없음.
- 검증 근거:
  - 각 언어별 surface test 또는 sample이 Actor public API 이름을 직접 사용한다.
  - .NET multi perf harness 변경 뒤 `dotnet build`가 성공했다.
- 남은 확인:
  - 같은 범위를 한 번 더 반복해서 mismatch가 없는지 확인한다.

## 2026-05-05 bindings 코드/spec 반복 리뷰 2차

- 대상:
  - C, C++, Rust, Go, Java, Node, Python, .NET binding spec과 구현
- 수행한 확인:
  - 1차와 같은 검색을 반복했다.
  - `doc/spec/bindings`와 binding 구현/테스트/sample에서 제거된 Actor 설계 이름과
    잘못된 generation 의미가 남아 있지 않은지 다시 확인했다.
  - .NET perf-only 수정이 public API spec과 충돌하지 않는지 확인했다.
- 발견한 mismatch:
  - 없음.
- 결론:
  - 모든 언어 binding은 두 번 연속 spec/code mismatch가 없다.

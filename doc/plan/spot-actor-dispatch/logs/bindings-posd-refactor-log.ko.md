# Bindings POSD Refactor Log

- 날짜: 2026-05-04
- 대상: bindings
- 수행한 명령: 없음
- 발견한 문제: bindings 적용 전
- 수정한 파일: 없음
- 남은 위험: 언어별 binding 구현 뒤 POSD 반복 리뷰 필요
- 다음 확인: C, C++, Rust, Go, Java, Node, Python, .NET 순차 진행

## 2026-05-05 bindings POSD 반복 리뷰

- 대상:
  - C, C++, Rust, Go, Java, Node, Python, .NET binding 구현과 sample/perf wrapper
- 기준:
  - `doc/principal/software-design-principles.md`
- 위험 신호와 판단:
  - 얕은 pass-through 증가 위험: Actor API는 core C 함수를 그대로 노출해야 하는
    부분이 있지만, 각 언어는 ownership, result/error mapping, unchecked ref 의미를
    언어별 타입으로 감싸 호출자 복잡성을 줄인다.
  - 정보 누출 위험: Actor readable dispatch는 callback context에서 drain해야 하는
    제약이 있다. Java/Node/.NET은 native callback 진입 시점에 preloaded part로
    흡수해 public callback 사용자가 thread 제약을 알 필요 없게 했다.
  - 중복 지식 위험: unchecked remote ref 생성은 각 언어의 ActorRef helper 한 곳에
    모으고, generation `0` 의미를 여러 call site에 흩뜨리지 않았다.
  - 특수/범용 혼합 위험: .NET multi SPOT perf의 stale route 회피는 public binding
    API가 아니라 perf harness 내부의 service-name helper와 runner cooldown option에
    가뒀다.
- 검토한 대안:
  - 대안 1: 언어별 binding에 core 함수와 같은 이름의 thin wrapper를 모두 추가한다.
    표면은 단순히 빠르게 맞지만 ownership과 error/result 의미가 호출자에게 새어
    얕은 모듈이 된다.
  - 대안 2: ActorRef, Actor, route, join request, dispatch info 같은 언어별 타입을
    두고 core 세부 사항을 내부 interop 계층에 숨긴다.
  - 선택: 대안 2. public surface는 언어별 관례를 따르고, core 계약 지식은 binding
    내부와 surface test에 모인다.
- 1차 결과:
  - C: native C 계약을 그대로 노출하는 얇은 계층이므로 추가 추상화 대상 아님.
  - C++: RAII type이 handle ownership을 숨기며 Actor API가 service namespace 안에
    모여 있다.
  - Rust: Result/Drop/owned Message wrapper로 ownership과 error mapping을 숨긴다.
  - Go: exported type과 error type으로 C result code를 직접 노출하지 않는다.
  - Java: ActorInterop이 native layout 지식을 모으고 public record가 값 의미를
    제공한다.
  - Node: native addon과 TypeScript facade가 callback thread 제약을 숨긴다.
  - Python: dataclass/facade가 FFI layout을 숨기고 sample은 public API만 사용한다.
  - .NET: ActorInterop과 SpotDispatchInfo pre-drain이 native callback 제약을 숨긴다.
- 2차 결과:
  - 새 POSD 리팩토링 후보 없음.
  - 언어별 sample은 내부 endpoint, native layout, callback thread 세부 사항에
    의존하지 않는다.
  - perf wrapper 변경은 .NET multi SPOT harness 내부에 한정되어 public API 표면을
    늘리지 않았다.
- 검증:
  - 각 언어별 test/sample/perf 결과는 `bindings-update-log.ko.md`에 기록했다.
  - .NET multi perf 추가 검증은 complete full run과 `MULTI_SPOT/tcp/64` 단독
    complete run으로 닫았다.
- 결론:
  - 모든 언어에서 두 번 연속 새 POSD 리팩토링 후보가 없다.

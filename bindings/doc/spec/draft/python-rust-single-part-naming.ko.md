# Python·Rust 단일 part 접근 이름 초안

> 이 문서는 구현 전 초안이며 현재 공개 계약이 아니다. 관련 정식 spec이 갱신되고 구현과 contract test가
> 통과하기 전에는 public method 이름의 근거로 사용하지 않는다.

## 1. 해결할 문제

공통 spec은 단일 part를 꺼내는 이름으로 `single_part_or_throw()`를 사용한다. Python은 exception을
`raise`한다고 표현하므로 `throw`가 관례와 맞지 않는다. Rust 구현에는 `single_part()`와
`single_part_or_error()`가 함께 있어 `Result`가 이미 나타내는 실패 가능성을 이름에서 반복한다.

## 2. 권고안

Python과 Rust 모두 `single_part()`를 사용한다.

- Python은 part가 정확히 하나면 `Message`를 반환하고, 아니면 `RecvError`를 raise한다.
- Rust는 `Result<Message, RecvError>`를 반환한다. 성공하면 receiver가 소유하던 part를 호출자에게 옮긴다.
- 첫 part를 borrow하는 `first_part()`와 전체 part를 소유권 이전하는 `into_parts()`는 별도 동작으로 유지한다.
- 이전 이름을 deprecated alias나 compatibility wrapper로 남기지 않는다.

Python의 `Message.from_(...)`와 `RoutingId.from_(...)`는 예약어를 피하기 위한 정식 이름이므로 이 초안의 변경
대상이 아니다.

## 3. 검토한 대안

`single_part_or_raise()`와 `single_part_or_error()`를 언어별로 유지하는 안도 검토한다. 실패 전달 방식은
분명하지만 반환 타입과 예외 계약이 이미 실패를 표현하고, 같은 개념의 이름이 언어마다 불필요하게 길어진다.
따라서 짧은 `single_part()`를 권고한다.

## 4. 승인 조건

1. 권고 이름과 현재 정식 spec·구현의 차이가 implementation gap에 기록된다.
2. 빈 값, 한 part와 여러 part를 검증하는 public contract test를 두 언어에 추가한다.
3. 성공과 실패 뒤 part 및 receiver ownership을 검증한다.
4. 구현과 contract test가 통과한 뒤 공통 spec과 Python·Rust 언어별 정식 spec에 이 초안의 exact interface를
   나누어 반영한다.

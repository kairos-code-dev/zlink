# Node RegistrationCodec E2E feature map

이 문서는 Config 4 Registration/Codec 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RC-A3`: public NestJS builder의 수동 request handler 등록으로 request/reply가 동작한다.
- `RC-B1`: public codec builder의 JSON codec 등록 뒤 request/reply payload가 round-trip된다.

## public API/harness 대기

- `RC-A1`: 자동 등록 Node runner와 marker가 아직 없다.
- `RC-A2`: decorator/annotation 등록 Node runner와 marker가 아직 없다.
- `RC-A4`: DI lifecycle Node runner와 marker가 아직 없다.
- `RC-A5`: filter ordering Node runner와 marker가 아직 없다.
- `RC-A6`: 잘못된 등록 차단 Node runner와 marker가 아직 없다.
- `RC-B2`: Protobuf codec Node runner와 marker가 아직 없다.
- `RC-B3`: MessagePack codec Node runner와 marker가 아직 없다.
- `RC-B4`: codec 공존 Node runner와 marker가 아직 없다.
- `RC-B5`: peer codec mismatch Node runner와 marker가 아직 없다.

# C++ Registration/Codec E2E feature map

이 문서는 공통 시나리오 중 C++ framework의 공개 API로 직접 검증할 수 있는 항목과
검증 대상에서 제외한 항목을 구분한다.

## 구현한 시나리오

- `RC-A1`: `options.handlers().add<THandler>(group)`와 `use_handler_group(group)`로
  handler group 기반 request/send 등록을 검증한다. C++에서는 packet 타입의
  `static constexpr packet_name`이 공개 packet 이름이 된다.
- `RC-A3`: route mesh builder의
  `add_request_handler<TOwner, TRequest, TReply>("EchoManual", ...)`로 명시 packet 이름을
  등록하고 실제 route request로 검증한다.
- `RC-A6`: 잘못된 설정이 startup에서 실패하는지 검증한다. 중복 handler 등록,
  fanout channel에 send handler group을 연결하는 잘못된 조합, handler group 없이
  client/server server를 여는 조합을 별도 프로세스로 실행한다.
- `RC-B1`: JSON serializer roundtrip을 검증한다.
- `RC-B4`: JSON serializer와 custom serializer가 같은 channel에서 공존하는지 검증한다.
- `RC-B5`: client와 server가 서로 다른 reply serializer를 등록했을 때 typed request가
  decode failure로 실패하는지 검증한다.

## C++에서 제외한 시나리오

- `RC-A2`: attribute 기반 handler discovery는 `.NET`의 attribute 표면이다. C++에는
  런타임 attribute나 assembly scan 공개 API가 없으므로 `RC-A1`의 handler group 등록이
  C++의 대응 표면이다.
- `RC-B2`, `RC-B3`: C++ framework에는 protobuf/messagepack extension 헤더가 있지만 현재
  channel message envelope의 공개 content type은 `application/json`으로 고정되어 있다.
  extension은 payload serializer 등록만 제공하므로 `application/x-protobuf` 또는
  messagepack content type을 공개 channel API로 검증할 수 없다.

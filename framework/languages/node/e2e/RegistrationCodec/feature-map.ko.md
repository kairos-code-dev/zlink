# Node RegistrationCodec E2E feature map

이 문서는 Config 4 Registration/Codec 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RC-A1`: public provider discovery가 fixture module의 request/send handler를 자동 등록하고 evidence를 남긴다.
- `RC-A2`: public NestJS decorator와 handler group 등록으로 request/send evidence를 확인한다.
- `RC-A3`: public NestJS builder의 수동 request/send handler 등록으로 reply와 send evidence를 확인한다.
- `RC-A5`: public `options({ filters: [...] })` 등록이 NestJS DI에서 filter를 resolve하고, request handler
  호출 전후 순서를 보존하는지 확인한다.
- `RC-A6`: duplicate handler와 invalid handler group이 registration/startup 단계에서 실패하는지 확인한다.
- `RC-B2`: public Protobuf codec extension을 단일 codec registry에 등록하고, request/send handler context
  content-type과 typed reply round-trip을 확인한다.
- `RC-B3`: public MessagePack codec extension을 단일 codec registry에 등록하고, request/send handler context
  content-type과 typed reply round-trip을 확인한다.

## public API/harness 대기

- `RC-A4`: Node NestJS 통합은 handler 호출 때 public Nest `ModuleRef.get(...)`으로 provider instance를
  가져온다. 현재 public contract에는 dispatch마다 scoped 의존성을 새로 만들고 dispose하는 보장이
  없으므로, 공통 lifecycle 시나리오는 Node에서 완료로 표시하지 않는다.
- `RC-B1`: public reply content-type evidence가 없어 JSON codec 전체 marker는 아직 두지 않는다.
- `RC-B4`: codec 공존 Node runner와 marker가 아직 없다.
- `RC-B5`: peer codec mismatch Node runner와 marker가 아직 없다.

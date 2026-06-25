# Node RegistrationCodec E2E feature map

이 문서는 Config 4 Registration/Codec 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RC-A1`: public provider discovery가 fixture module의 request/send handler를 자동 등록하고 evidence를 남긴다.
- `RC-A2`: public NestJS decorator와 handler group 등록으로 request/send evidence를 확인한다.
- `RC-A3`: public NestJS builder의 수동 request/send handler 등록으로 reply와 send evidence를 확인한다.
- `RC-A5`: public `options({ filters: [...] })` 등록이 NestJS DI에서 filter를 resolve하고, request handler
  호출 전후 순서를 보존하는지 확인한다.
- `RC-A6`: duplicate handler와 invalid handler group이 registration/startup 단계에서 실패하는지 확인한다.
- `RC-B1`: 기본 JSON codec으로 request/send handler context content-type과 typed reply round-trip을 확인한다.
- `RC-B2`: public Protobuf codec extension을 단일 codec registry에 등록하고, request/send handler context
  content-type과 typed reply round-trip을 확인한다.
- `RC-B3`: public MessagePack codec extension을 단일 codec registry에 등록하고, request/send handler context
  content-type과 typed reply round-trip을 확인한다.

## public API/harness 대기

- `RC-A4`: Node NestJS 통합은 handler 호출 때 public Nest `ModuleRef.get(...)`으로 provider instance를
  가져온다. 현재 public contract에는 dispatch마다 scoped 의존성을 새로 만들고 dispose하는 보장이
  없으므로, 공통 lifecycle 시나리오는 Node에서 완료로 표시하지 않는다.
- `RC-B4`: 공통 spec과 Node channel spec은 `codecs().use(...)` 기반 extension 등록과
  Protobuf/MessagePack package 위치를 공개한다. 그러나 현재 Node public serializer 계약에는
  여러 serializer가 함께 등록됐을 때 payload 타입 또는 content-type으로 송신 serializer를 고르는
  정책이 없다. runtime도 serializer가 둘 이상이면 payload serializer 선택을 모호성 오류로 처리한다.
  따라서 새 public 선택 정책을 이 E2E 작업에서 만들지 않고, spec/guide에서 다중 codec 선택 규칙이
  확정되기 전까지 완료로 표시하지 않는다.
- `RC-B5`: peer 간 codec registry 불일치에 대해 Node public spec/guide가 fallback 또는 error
  규칙을 공개 계약으로 고정하지 않는다. runtime 내부에는 decode failure 처리 경로가 있지만, 현재
  공통 정책상 다른 언어 구현이나 내부 동작만 근거로 public contract를 추가하지 않는다. 계약이
  확정되기 전까지는 완료로 표시하지 않고 public contract gap으로 남긴다.

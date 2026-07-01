# handler registration annotation 공개 계약 초안

> 이 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, 언어별 framework 구현에 바로 추가할
> API를 확정하지 않는다.

## 배경

공통 E2E Config 4의 `RC-A2`는 annotation 또는 decorator로 표시한 request/send handler가 자동 등록과
수동 등록 경로와 같은 reply와 evidence 의미를 내는지 검증한다.

.NET framework는 `[ZLinkHandlerGroup]`, `[ZLinkRequest]`, `[ZLinkSend]` attribute를 사용한다. C++
framework는 현재 packet type의 `packet_name`, handler group builder, 명시 handler 등록 표면을 제공하지만
C++ 언어 표면에 대응하는 annotation/decorator 계약은 없다.

따라서 C++ `RC-A2`를 helper macro, 테스트 전용 adapter, 별도 scan 규칙으로 통과시키면 공통 E2E가
요구하는 공개 등록 계약을 검증하지 못한다.

## 필요한 공개 의미

annotation/decorator 등록 계약을 받아들이려면 framework 공통 계약은 아래 의미를 먼저 정해야 한다.

- 언어별로 handler group 이름과 packet 이름을 handler type 또는 handler member에 선언할 수 있어야 한다.
- 선언된 request/send handler는 자동 등록과 수동 등록 경로와 같은 duplicate validation을 받아야 한다.
- 등록 결과는 수동 등록과 같은 dispatch, DI lifecycle, filter ordering, codec 선택 규칙을 따라야 한다.
- 언어별로 reflection이 없는 경우에도 같은 public 의미를 제공할 수 있는 mapping을 정해야 한다.
- annotation/decorator 표면이 없는 언어는 완료로 표시하지 않고 public contract gap으로 남겨야 한다.

## 설계 후보

### 후보 A: C++ macro annotation을 공개한다

handler type 안이나 옆에 macro로 handler group, packet name, handler kind를 선언한다. 기존 C++ 컴파일
모델과 맞지만 macro가 public API가 되므로 이름, scope, duplicate validation 시점을 명확히 정해야 한다.

### 후보 B: trait 기반 metadata를 공개한다

handler type별 trait specialization으로 group, packet name, handler kind를 선언한다. macro보다 C++답고
테스트하기 쉽지만, 사용자 코드가 trait boilerplate를 작성해야 한다.

### 후보 C: 현재 명시 등록을 C++ mapping으로 인정한다

C++에는 annotation/decorator 표면을 추가하지 않고, `RC-A1` 자동 등록과 `RC-A3` 명시 등록을 C++
mapping으로 인정한다. 이 경우 공통 E2E 문서에서 `RC-A2`의 언어별 mapping 기준을 수정해야 하며, C++은
`RC-A2`를 완료가 아니라 “해당 언어 표면 없음”으로 보고해야 한다.

## 현재 판정

`RC-A2`는 공통 E2E의 P0 scenario이므로 C++에서 완료로 표시하지 않는다. 다만 C++에 새 public API를
바로 추가하지도 않는다. 먼저 이 draft를 기준으로 공통 framework 계약과 C++ spec/guide를 갱신할지
리뷰한 뒤, 확정된 공개 표면으로 C++ E2E를 구현한다.

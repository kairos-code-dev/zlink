# S3 iteration 8 finding ledger

## 1. 집계

| 항목 | 값 |
|---|---:|
| Codex raw finding | 9 |
| Claude Sonnet raw finding | 39 |
| 고유 finding | 48 |
| 채택·수정 | 45 |
| 기각 | 3 |
| open | 0 |

공통 E2E·sample은 새 public API의 근거로 사용하지 않았다. 현재 구현 lag와 10.0.0 target 계약을 분리해 판정했다.

## 2. Finding과 closure

| ID | Reviewer | 문제 | 상태 | Closure evidence |
|---|---|---|---|---|
| I8-CX-01 | Codex | .NET request·Actor request·join Yield 누락 | resolved | .NET exact에 Yield 추가 |
| I8-CX-02 | Codex | C++ 일반 request yield 누락 | resolved | request_call_t에 yield 추가 |
| I8-CX-03 | Codex | C++ channel_request_call_t 미선언 | resolved | 완전한 call type 추가 |
| I8-CX-04 | Codex | C++ Connector close reason 누락 | resolved | reason과 callback 추가 |
| I8-CX-05 | Codex | Java SpotService stale SpotRef | resolved | SpotHandle로 정렬 |
| I8-CX-06 | Codex | Kotlin SpotService stale SpotRef | resolved | SpotHandle로 정렬 |
| I8-CX-07 | Codex | 공통 Config 2 SM-F6 누락 | resolved | 공통과 .NET 행 추가 |
| I8-CX-08 | Codex | Java unsigned 64-bit 규칙 누락 | resolved | unsigned 해석 규칙 추가 |
| I8-CX-09 | Codex | ShoppingMall 구어체·의인화 | resolved | 중립적 표현으로 교체 |
| I8-CL-01 | Claude | Config 9 Core draft 근거 | resolved | Actor 모델 §5로 교체 |
| I8-CL-02 | Claude | Config 3 topic 계약·죽은 인용 | resolved | packet-name dispatch와 05 §11로 정렬 |
| I8-CL-03 | Claude | Config 7 legacy monitoring 충돌 | resolved | MeshNode runtime monitoring으로 재작성 |
| I8-CL-04 | Claude | Kotlin Observability A2 과대 완료 | resolved | 미완료 증거 반영 |
| I8-CL-05 | Claude | Kotlin SpotService 수신 근거 모순 | resolved | 목표와 현재 증거 분리 |
| I8-CL-06 | Claude | Node RegistryMessaging 전체 완료 불가 | rejected | RM-C7 외 canonical 의미 일치 |
| I8-CL-07 | Claude | Node RM-C7 구현 API 근거 오류 | resolved | target과 현재 상태 분리 |
| I8-CL-08 | Claude | Node Discovery endpoint 불일치 | rejected | same-endpoint도 새 socket outbound 억제 검증 |
| I8-CL-09 | Claude | Java transfer ST-A1 과대 완료 | resolved | 전환 대상으로 정정 |
| I8-CL-10 | Claude | Java Observability A1 과대 완료 | resolved | 현재 verifier gap 반영 |
| I8-CL-11 | Claude | C++ session handshake_failed 충돌 | resolved | session error에서 제거 |
| I8-CL-12 | Claude | C++ HTTP kind/code 혼합 | resolved | 두 오류 채널 분리 |
| I8-CL-13 | Claude | C++ allocated RID provider 누락 | resolved | provider exact type 추가 |
| I8-CL-14 | Claude | C++ disconnect actor 시그니처 누락 | resolved | 정식 멤버 추가 |
| I8-CL-15 | Claude | Java close reason 참조 오류 | resolved | Connector 계약으로 교정 |
| I8-CL-16 | Claude | Node catalog header 범위 오류 | resolved | 실제 선언 범위로 수정 |
| I8-CL-17 | Claude | Bingo 언어별 절 인용 오류 | resolved | 실제 interface 절로 교정 |
| I8-CL-18 | Claude | sample handler 등록 절 오류 | resolved | 05 §8로 교정 |
| I8-CL-19 | Claude | Connector flow 절 인용 오류 | resolved | 53 실제 절로 교정 |
| I8-CL-20 | Claude | TypeScript flowFrom 절 오류 | resolved | Async context 절로 교정 |
| I8-CL-21 | Claude | C++ Connector flow 누락 | resolved | capture·propagation 추가 |
| I8-CL-22 | Claude | Java Connector flow 누락 | resolved | 명시적 flowFrom 추가 |
| I8-CL-23 | Claude | 공통 E2E Location Store 인용 오류 | resolved | exact 등록 계약으로 교정 |
| I8-CL-24 | Claude | Config 6 Location 인용 오류 | resolved | owner lease·exact 근거로 교정 |
| I8-CL-25 | Claude | Config 5 drain 인용 오류 | resolved | selection·admission 절로 교정 |
| I8-CL-26 | Claude | Config 11 flow·metric 인용 오류 | resolved | 실제 flat 절로 교정 |
| I8-CL-27 | Claude | Config 10 transfer·correlation 인용 오류 | resolved | §10.4·flow 근거로 교정 |
| I8-CL-28 | Claude | Kotlin PubSub 과대 완료 | resolved | ConnectionReady 미완료 반영 |
| I8-CL-29 | Claude | Node SpotService TLS 생략 미표기 | resolved | 완료 근거에서 제외 |
| I8-CL-30 | Claude | Java transfer 해결 finding을 open 인용 | resolved | 현재 상태로 갱신 |
| I8-CL-31 | Claude | Java Observability A2 stale 상태 | resolved | live evidence와 정렬 |
| I8-CL-32 | Claude | Java StoreFailure stale blocker | rejected | formal target과 부분 구현 표기 일치 |
| I8-CL-33 | Claude | Java ToActor TA-B1 stale blocker | resolved | 해결 상태 반영 |
| I8-CL-34 | Claude | timer backend에 C framework 표기 | resolved | C++와 Core C API 분리 |
| I8-CL-35 | Claude | Node disconnect actor optional | resolved | 필수 멤버로 정렬 |
| I8-CL-36 | Claude | Bingo governance 절 오류 | resolved | 00 §4로 교정 |
| I8-CL-37 | Claude | meter 이름 대소문자 불일치 | resolved | zlink.framework로 통일 |
| I8-CL-38 | Claude | flow protocol error mapping 불명확 | resolved | 닫힌 error mapping 명시 |
| I8-CL-39 | Claude | Java Registry changelog 서술 | resolved | 현재 상태 설명으로 교체 |

## 3. Closure gate

- Framework verifier: exact 24개, formal 48개, 공개 선언 1,161개, feature map 55개, scenario row 955개.
- Scope structure: 177개, link 814개, table block 336개, fence 문서 81개, 오류 0.
- E2E·sample·runner·guide inventory: 55·32·4·96·81 전부 존재한다.
- scoped diff check, plan 참조 검사와 JSON parse가 통과했다.
- 수정본은 iteration 9에서 새 aggregate로 동결했다.

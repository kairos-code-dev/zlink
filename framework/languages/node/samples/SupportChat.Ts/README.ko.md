# SupportChat TypeScript Sample

SupportChat TypeScript 샘플은 .NET SupportChat 정본의 상담 지원 흐름을 Node 샘플 구조로 옮긴다. 한 명의 상담원이 여러 고객 대화를 처리하고, 고객별 대화 상태와 메시지 순번이 서로 섞이지 않는지 클라이언트 시나리오에서 검증한다.

## 실행

```bash
cd framework/languages/node/samples/SupportChat.Ts
./run_sample.sh
```

`run_sample.sh`는 샘플을 빌드한 뒤 API 역할을 별도 프로세스로 띄우고, 클라이언트 시나리오를 실행한다. 실행이 끝나면 `PASS SupportChat.Ts`가 출력된다.

## Topology

```text
+------------------+     +------------------+
| Client Scenario  | --> | SupportChat API  |
+------------------+     +------------------+
          |                       |
          v                       v
+------------------+     +------------------+
| Session Role     |     | Support Role     |
+------------------+     +------------------+
```

API 역할은 실행 가능한 검증 경로를 제공한다. Support와 Session 역할 파일은 .NET 정본의 역할 분리를 Node 샘플 구조 안에서 드러내며, 계약과 handler 이름은 같은 시나리오 흐름을 가리킨다.

## Success Condition

클라이언트 시나리오는 다음 조건을 모두 확인한다.

- 상담원 인증과 availability 등록이 성공한다.
- 두 고객 대화가 같은 상담원에게 배정되고, 각 대화의 메시지 순번은 독립적으로 증가한다.
- typing 알림은 상대 참여자에게만 전달된다.
- 고객과 상담원이 다시 접속해도 기존 대화 상태와 마지막 메시지 순번을 읽을 수 있다.
- 하나의 대화는 명시적으로 닫히고, 다른 대화는 idle 상태를 거쳐 닫힌다.
- 닫힌 대화는 메시지와 typing 알림을 더 이상 전달하지 않는다.
- 상담원이 unavailable이면 새 고객 대화는 `WaitingForAgent` 상태로 남는다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`는 `SupportChat.Ts`를 required sample과 topology sample 목록에 포함한다. 이 테스트는 README, runner, inventory, 역할별 파일, public API 사용 규칙을 함께 확인한다.

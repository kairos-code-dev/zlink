# TicTacToe 샘플 적용 프롬프트

아래 프롬프트를 저장소 루트에서 작업하는 구현 에이전트에게 전달한다.

```text
먼저 다음 문서를 읽는다.

- AGENTS.md
- framework/doc/framework/common/sample/tictactoe/README.ko.md

목표:
C++, Java, Kotlin, Node TicTacToe 샘플을 공통 sample 문서의 public scenario와 맞춘다.
공개 Spot은 PlayEntrySpot과 TicTacToeGame만 둔다. observer milestone 처리를 위해
PlayerNotificationSpot 같은 별도 public Spot을 만들지 않는다. observer 목록 관리와
WinMilestoneNotify 전송은 PlayEntrySpot 내부 handler와 private registry가 담당한다.

필수 시나리오:
1. 2개 API와 2개 Play 프로세스를 실행한다.
2. Redis room route store를 사용한다. run_sample은 실행마다 전용 Docker Redis container를
   임시 localhost port로 시작하고 종료 시 자신이 만든 container만 정리한다.
3. 이미 실행 중인 Redis나 host Redis endpoint를 재사용하지 않는다. 실행별 key prefix는 같은 전용
   container 안에서 sample 내부 key를 구분하기 위한 값이며, 여러 실행이 Redis를 공유하기 위한
   장치가 아니다.
4. API는 CreateGameHttpReq에 room id, owner Play endpoint, Play endpoint 목록,
   Play endpoint별 MeshNode RID와 required level을 반환한다.
5. host는 owner Play에 연결하고 guest와 observer는 owner가 아닌 Play에 연결한다.
6. Play session 인증은 PlayerInfo를 반환한다. host player는 wins=99로 시작한다.
7. JoinSpot payload에는 PlayerInfo를 포함하고 TicTacToeGame은 RequiredLevel을 확인한다.
8. host 승리 후 TicTacToeGame은 PlayerWinMilestoneEvent를 public Logical Multicast API로 publish한다.
9. owner가 아닌 Play의 PlayEntrySpot은 milestone topic을 subscribe하고 observer actor에게
   WinMilestoneNotify를 push한다.
10. observer client는 WinMilestoneNotify(actor=player-x, wins=100,
    receivingMeshNodeRid=observer가 연결된 Play node RID)를 실제로 수신해야 한다.
11. 게임 종료 뒤 host와 guest가 LeaveGameReq(RoomId)를 보내고 room leave 후 Entry Spot의
    actor destroy 경로가 실행되어야 한다.

POSD 기준:
- handler, actor, Spot, domain에 Redis client 타입을 직접 노출하지 않는다.
- Redis route 저장과 조회는 route store adapter 안에 숨긴다.
- observer milestone 처리를 위해 새 public Spot을 만들지 않는다.
- room 상태와 승패 판정은 TicTacToeGame에 둔다.
- observer 등록과 milestone push fan-out은 PlayEntrySpot 내부 책임으로 둔다.
- 실패를 통과시키기 위한 sample-only 우회 helper를 만들지 않는다. framework나 binding
  결함이 발견되면 원래 책임이 있는 계층에 회귀 테스트를 추가하고 수정한다.

검증:
- 각 언어 run_sample script를 실행한다.
- 각 script는 observer-connected, observer-subscription=verified,
  observer-win-milestone=verified, LeaveGameReq completed, completed/PASS marker를 검증한다.
- C++는 framework/languages/cpp/build 기준 CTest gate도 함께 통과해야 한다.
- Java/Kotlin은 Gradle compile/test gate를 함께 통과해야 한다.
- Node는 npm build와 sample script를 함께 통과해야 한다.

완료 조건:
- README.ko.md와 구현이 같은 시나리오를 설명한다.
- C++, Java, Kotlin, Node sample 실행이 모두 실제 marker로 통과한다.
- Redis Docker container가 script 종료 후 남지 않는다.
```

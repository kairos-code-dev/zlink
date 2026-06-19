# TicTacToe 샘플 적용 프롬프트

아래 프롬프트를 `/home/hep7/project/kairos/zlink`에서 실행하는 작업자에게 전달한다.

```text
작업 위치: /home/hep7/project/kairos/zlink

기준 문서:
- framework/doc/framework/common/sample/tictactoe/README.ko.md

목표:
C++, Java, Kotlin, Node TicTacToe 샘플을 기준 문서의 public scenario와 맞춘다.
보이는 public Spot은 PlayEntrySpot과 TicTacToeGame만 둔다. observer milestone 처리를 위해
PlayerNotificationSpot 같은 별도 public Spot을 만들지 않는다. observer 목록 관리와
WinMilestoneNotify 전송은 PlayEntrySpot 내부 handler/private registry 책임으로 둔다.

필수 시나리오:
1. 2 API + 2 Play 프로세스를 실행한다.
2. Redis room route store를 사용한다. run_sample은 외부 Redis endpoint가 없으면 Docker
   Redis container를 임시 localhost port로 띄우고 종료 시 정리한다.
3. 외부 Redis를 쓰는 경우에도 실행별 key prefix를 사용해 다른 테스트와 route key가 섞이지
   않게 한다.
4. API는 CreateGameHttpReq에 room id, owner Play endpoint, Play endpoint 목록,
   Play endpoint별 SpotNode rid, required level을 반환한다.
5. host는 owner Play에 연결하고 guest와 observer는 owner가 아닌 Play에 연결한다.
6. Play session 인증은 PlayerInfo를 반환한다. host player는 wins=99로 시작한다.
7. JoinSpot payload에는 PlayerInfo를 포함하고 TicTacToeGame은 RequiredLevel을 확인한다.
8. host 승리 후 TicTacToeGame은 PlayerWinMilestoneEvent를 public Spot pub/sub API로 publish한다.
9. owner가 아닌 Play의 PlayEntrySpot은 milestone topic을 subscribe하고 observer actor에게
   WinMilestoneNotify를 push한다.
10. observer client는 WinMilestoneNotify(actor=player-x, wins=100,
    receivingSpotNodeRid=observer가 연결된 Play node rid)를 실제로 수신해야 한다.
11. 게임 종료 뒤 host와 guest가 LeaveGameReq(RoomId)를 보내고 room leave 후 Entry Spot
    destroy 경로가 실행되어야 한다.

POSD 기준:
- handler, actor, Spot, domain에 Redis client 타입을 직접 노출하지 않는다.
- Redis route 저장/조회는 route store adapter 안에 숨긴다.
- observer milestone 처리를 위해 새 public Spot을 만들지 않는다.
- room 상태와 승패 판정은 TicTacToeGame에 둔다.
- observer 등록과 milestone push fan-out은 PlayEntrySpot 내부 책임으로 둔다.
- 실패를 통과시키기 위한 sample-only 우회 helper를 만들지 않는다. framework나 binding
  버그가 발견되면 원인 레이어에 회귀테스트를 추가하고 고친다.

검증:
- 각 언어 run_sample script를 실행한다.
- 각 script는 observer-connected, observer-subscription=verified,
  observer-win-milestone=verified, LeaveGameReq completed, completed/PASS marker를 검증한다.
- C++는 framework/languages/cpp/build 기준 ctest gate도 함께 통과해야 한다.
- Java/Kotlin은 Gradle compile/test gate를 함께 통과해야 한다.
- Node는 npm build와 sample script를 함께 통과해야 한다.

완료 조건:
- README.ko.md와 구현이 같은 시나리오를 설명한다.
- C++, Java, Kotlin, Node sample run이 모두 실제 marker로 통과한다.
- Redis Docker container가 script 종료 후 남지 않는다.
- dirty worktree에서 관련 파일만 stage/commit/push한다.
```

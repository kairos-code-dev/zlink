# GameQuest C++ sample

GameQuest 샘플은 게임 플레이 이벤트를 GameApi stream endpoint로 받고, player id 기준 owner인
QuestMission role의 owner별 channel로 request를 보낸다. QuestMission은 quest projection을 갱신하고,
GameApi는 현재 연결된 stream session에 progress/completion notify를 돌려준다.

runner는 Redis location store를 공유 location store로 사용한다. 별도 registry process를 두지
않고, 두 GameApi role과 두 QuestMission role이 같은 Redis prefix 아래에서 route 위치를 찾는다.

`run_sample.sh`가 `PASS GameQuest.Cpp`와 `gamequest sample result=passed`를 출력하면 C++ sample의
빌드, role 실행, client scenario, server evidence, flow trace가 함께 통과한 것이다.

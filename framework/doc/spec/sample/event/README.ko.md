# Event Sample Scenarios

[샘플 목록](../README.ko.md)

이 디렉토리는 event 전파를 중심으로 framework 기능을 보여 주는 공통 샘플 시나리오를
정의한다. 두 샘플은 event를 다루지만 목적이 다르다.

이 디렉토리의 문서도 상위 sample spec과 같은 작성 기준을 따른다. `.NET` Bingo와
TicTacToe 샘플처럼 서버 역할, 연결 방식, 메시지 계약, 흐름, client 시나리오, 구현 완료
기준을 한 문서 안에서 확인할 수 있어야 한다. 차이는 event의 기준 경로뿐이다.
durable event가 필요한 업무 흐름은 Redis Stream 또는 Kafka를 기준 경로로 두고,
유실되어도 snapshot으로 보정할 수 있는 realtime 흐름은 ZLink fanout을 기준 경로로 둔다.

| 샘플 | 목적 | event 기준 경로 | ZLink 역할 |
|------|------|----------------|------------|
| [ShoppingMallCheckout](./shoppingmall-checkout.ko.md) | 주문, 재고, 결제처럼 event 저장이 필요한 웹서비스에서 ZLink를 함께 쓰는 구조를 보여 준다. | Redis Stream 또는 Kafka | channel, discovery, workflow Spot, stream notify |
| [GameQuest](./gamequest.ko.md) | 여러 gameplay subsystem에서 발생한 event를 구독해 quest 진행을 갱신한다. | ZLink fanout | realtime fanout, Quest Spot, stream notify |

ShoppingMallCheckout은 durable event broker가 필요한 경계를 명확히 보여 준다. ZLink는
event broker를 대체하지 않고 service command/query, endpoint discovery, 상태 소유,
client push를 맡는다.

GameQuest는 ZLink fanout이 자연스러운 샘플이다. combat, inventory, mission, world 같은
여러 영역에서 발생하는 event를 Quest 서버가 구독하고, `PlayerQuestSpot`이 quest 진행을
갱신한다. 영속성이 필요한 quest 진행 상태는 저장소에 남기고, 누락 가능성은 snapshot
재동기화로 보정한다.

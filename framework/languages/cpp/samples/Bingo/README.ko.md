# Bingo

이 샘플은 C++ framework의 channel/SPOT 중심 기본 실시간 메시징 흐름을 검토하기 위한
리뷰 샘플이다.

포함 범위는 아래와 같다.

- app/host bootstrap
- DI service와 hosted service lifecycle
- channel request/reply handler
- outbound-only host 구성
- manual channel connection
- publish/subscribe 구성과 일반 event publish
- callback submit
- coroutine submit
- handler error와 failure observer
- user Spot 생성
- SPOT timer 등록
- monitoring source 등록
- graceful shutdown
- offload handler option

이 샘플은 STREAM ActorGateway relay를 다루지 않는다. STREAM과 ActorGateway 기준 흐름은
`TicTacToe` 샘플에서만 다룬다.

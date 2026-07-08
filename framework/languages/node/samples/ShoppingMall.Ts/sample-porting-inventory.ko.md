# ShoppingMall.Ts 포팅 인벤토리

정본은 `framework/languages/dotnet/samples/ShoppingMall`이다. Node 샘플은 클라이언트가
`ZLinkHttpClient`로 두 Commerce API 인스턴스에 요청하고, Commerce API가 Redis location store로
발견한 OrderWorkflow channel에 workflow request를 보낸다. OrderWorkflow role은 주문 ID별
`OrderWorkflowSpot`을 만들고, workflow request를 SpotMesh의 `SpotRef` 대상 request로 넘겨 같은 주문의
상태 전이를 한 owner 흐름에서 처리한다.

| 항목 | Node 위치 | 상태 |
|------|-----------|------|
| 공유 주문 계약 | `Shared/Contracts/messages.ts` | done |
| 클라이언트 시나리오 | `Client/shoppingmall-client-scenario.ts` | done |
| 클라이언트 진입점 | `Client/main.ts` | done |
| Commerce API role | `Server/main.ts --role api-a`, `Server/main.ts --role api-b` | done |
| Order workflow role | `Server/main.ts --role workflow-a`, `Server/main.ts --role workflow-b` | done: workflow channel server, request handler group, `OrderWorkflowSpot` SpotMesh를 제공한다. |
| Redis location store | `Server/Configuration/location-store.ts` | done: CommerceApi와 OrderWorkflow role이 같은 Redis location store prefix를 공유하고, CommerceApi는 workflow channel을 discovery로 호출한다. |
| 주문 저장소 | `Server/Shared/Store/order-store.ts` | done |
| 실행 스크립트 | `run_sample.sh`, `run_sample.ps1` | done: 전용 Redis, `workflow-a`, `workflow-b`, `api-a`, `api-b`를 별도 Node process로 띄운 뒤 client 검증을 실행한다. PASS: `timeout 360s framework/languages/node/samples/ShoppingMall.Ts/run_sample.sh` |

검증 범위는 정상 주문, 멱등성, 동시 시작 경쟁, pending 복구, 명시 재개, 재고 실패, 결제 실패,
projection rebuild, 조회 일관성, scale-out 증거 확인이다. JSON payload에는 `BigInt`를 넣지 않는다.

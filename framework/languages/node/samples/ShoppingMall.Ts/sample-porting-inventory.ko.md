# ShoppingMall.Ts 포팅 인벤토리

정본은 `framework/languages/dotnet/samples/ShoppingMall`이다. Node 샘플은 클라이언트가
`ZLinkHttpClient`로 두 Commerce API 인스턴스에 요청하는 구조를 따른다.

| 항목 | Node 위치 | 상태 |
|------|-----------|------|
| 공유 주문 계약 | `Shared/Contracts/messages.ts` | done |
| 클라이언트 시나리오 | `Client/shoppingmall-client-scenario.ts` | done |
| 클라이언트 진입점 | `Client/main.ts` | done |
| 서버 진입점 | `Server/main.ts` | done |
| 주문 저장소 | `Server/Shared/Store/order-store.ts` | done |
| 실행 스크립트 | `run_sample.sh`, `run_sample.ps1` | done |

검증 범위는 정상 주문, 멱등성, 동시 시작 경쟁, pending 복구, 명시 재개, 재고 실패, 결제 실패,
projection rebuild, 조회 일관성, scale-out 증거 확인이다. JSON payload에는 `BigInt`를 넣지 않는다.

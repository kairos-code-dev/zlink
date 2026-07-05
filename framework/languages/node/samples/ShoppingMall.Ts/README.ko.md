# ShoppingMall TypeScript 샘플

`ShoppingMall.Ts`는 주문 생성과 주문 workflow 상태 전이를 Node.js에서 실행하는 샘플이다.
클라이언트는 `ZLinkHttpClient`로 두 Commerce API 인스턴스에 요청을 보내고, 서버는 실행별
작업 디렉터리에 주문 상태와 projection 상태를 저장한다.

## 실행

Linux 또는 WSL에서 전체 시나리오를 실행한다.

```bash
./run_sample.sh
```

Windows PowerShell에서는 다음 명령을 사용한다.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\run_sample.ps1
```

## Topology

```text
+------------------+       +------------------+
| Client scenario  | ----> | Commerce API A   |
| ZLinkHttpClient  | ----> | Commerce API B   |
+------------------+       +------------------+
                              |
                              v
                         +------------+
                         | Order store|
                         +------------+
```

클라이언트는 HTTP 공개 표면만 사용한다. 서버 두 인스턴스는 같은 파일 저장소를 공유해서
멱등 주문, pending 복구, projection rebuild, scale-out 읽기 흐름을 같은 실행 안에서 확인한다.

## Success Condition

시나리오는 정상 주문, 멱등성, 같은 멱등 키의 동시 시작 경쟁, pending 상태 복구, 명시 재개,
재고 실패, 결제 실패, projection rebuild, 일관성, scale-out 경로를 검증한다. 클라이언트 로그에
`shoppingmall=completed`와 `PASS ShoppingMall.Ts`가 출력되고, 서버 증거 확인이 끝나면
`shoppingmall-server-evidence=completed`가 출력된다.

## 회귀 테스트

`test/contract/sample-regression.test.js`는 이 샘플의 디렉터리, 실행 스크립트, README, inventory,
주요 역할 파일을 확인한다. 루트 `samples/run_samples.sh`는 다른 Node 샘플과 함께 이 샘플을 실행한다.

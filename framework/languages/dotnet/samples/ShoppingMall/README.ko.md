# ShoppingMall 샘플

`ShoppingMall`은 주문 생성과 주문 workflow 상태 전이를 보여주는 .NET Framework
샘플이다. Commerce API 역할은 주문 요청을 받고, OrderWorkflow 역할은 주문별 상태를
진행시키며, 클라이언트는 성공, 실패, 재시도, projection rebuild 흐름을 검증한다.

## 실행

Linux 또는 WSL에서 전체 시나리오를 실행한다.

```bash
./run_sample.sh
```

Windows PowerShell에서는 다음 명령을 사용한다.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\run_sample.ps1
```

## 구성

- `Shared/`는 클라이언트와 서버가 공유하는 주문 요청, 응답, 상태 계약을 담는다.
- `Client/`는 주문 workflow self-check 시나리오를 실행한다.
- `Server/CommerceApi/`는 외부 주문 요청을 받고 ZLink channel로 workflow에 전달한다.
- `Server/OrderWorkflow/`는 주문 상태 전이, 실패 처리, projection rebuild를 담당한다.
- `Server/Shared/`는 서버 역할 사이에서 공유하는 저장소와 도메인 코드를 담는다.
- `Server/Registry/`는 샘플 실행 중 사용할 discovery registry를 띄운다.

## 성공 조건

클라이언트 시나리오는 정상 주문, 멱등성, pending 상태, 재고 실패, 결제 실패,
projection rebuild, 일관성, scale-out 경로를 검증한다. `run_sample.sh`는 client
log에서 `shoppingmall=completed`를 확인하고, 서버 evidence 확인이 끝나면
`shoppingmall-server-evidence=completed`를 출력한다.

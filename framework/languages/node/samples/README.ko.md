# ZLink Node.js Framework Samples

이 디렉터리에는 Node.js/NestJS framework의 공개 API를 사용하는 여섯 가지 샘플이 있다.
각 샘플은 업무 흐름과 서버 역할이 다르며, 공통 시나리오의 정식 설명은
[framework 공통 sample 문서](../../../doc/framework/common/sample/README.ko.md)를 따른다.

개별 `run_sample.sh`와 `run_sample.ps1`은 샘플 이름만 공통 Node 실행기에 넘긴다. 공통 실행기는
빌드, 임시 설정 생성, 서버 시작, 연결 준비 확인, client 시나리오 실행과 종료 정리를 수행한다.
Redis는 실행마다 전용 Docker container를 만들고 종료할 때 제거한다. 샘플 runner는 client
시나리오의 성공 여부만 판정하며 framework 내부 metric이나 lifecycle은 별도 E2E와 contract test에서
검증한다.

## 샘플 목록

| 샘플 | 간략한 설명 | client 실행 환경 |
|------|-------------|------------------|
| `Bingo.Ts` | Session, player actor와 room Spot을 사용해 매칭, 게임 진행, 보상 알림과 actor handoff를 확인한다. Protobuf payload를 사용한다. | Chromium |
| `TicTacToe.Ts` | 두 API와 두 Play 서버를 사용해 수동 endpoint scale-out과 실시간 게임 흐름을 확인한다. | Chromium |
| `SupportChat.Ts` | 고객과 상담원의 대화 배정, reconnect, idle timer와 종료 알림을 확인한다. | Chromium |
| `DeliveryDispatch.Ts` | 배송 배차, timeout 재배정과 고객·기사 상태 알림을 확인한다. | Chromium |
| `GameQuest.Ts` | player별 quest owner Spot, event sourcing과 조회 모델 갱신을 확인한다. | Chromium |
| `ShoppingMall.Ts` | 주문 workflow의 event sourcing과 HTTP 조회 모델을 확인한다. | Node.js HTTP client |

Stream Connector를 사용하는 client는 브라우저용 ESM bundle로 만들어 실제 Chromium에서
실행한다. Node.js는 이 bundle을 만들고 정적 파일 서버와 headless Chromium을 실행하는 자동화
도구이며, Stream Connector의 client runtime이 아니다. 예를 들어
`Bingo.Ts/Client/bingo-client-scenario.ts`는 직접 `node`로 실행하지 않고 Bingo runner가
`Client/main.ts`와 함께 bundle해 Chromium에서 호출한다. 자세한 connector 사용법은
[TypeScript Stream Connector guide](../../../doc/stream-connector/typescript/README.ko.md)를 참고한다.

## 실행 준비

저장소의 Node framework workspace에서 dependency와 Chromium을 준비한다.

```bash
cd framework/languages/node
npm ci
npm run browser:install
```

`package.json`이 참조하는 bindings local package가 아직 없다면 먼저
[local package 배포 안내](../../../../scripts/local-package/README.ko.md)에 따라 package를 만든다.
Docker를 실행할 수 있어야 하며, Chromium을 설치할 때 운영체제 package가 추가로 필요하면
CI용 설치 명령인 `npm run browser:install:ci`를 사용할 수 있다.

## Linux 또는 WSL에서 실행

샘플 하나를 실행하려면 Node framework workspace에서 해당 runner를 호출한다.

```bash
cd framework/languages/node
./samples/Bingo.Ts/run_sample.sh
```

여섯 샘플을 순서대로 모두 실행하려면 다음 명령을 사용한다.

```bash
./samples/run_samples.sh
```

일부 샘플만 실행할 때는 디렉터리 이름을 인자로 넘긴다. 지정한 순서대로 실행된다.

```bash
./samples/run_samples.sh Bingo.Ts SupportChat.Ts
```

## Windows PowerShell에서 실행

```powershell
Set-Location framework/languages/node
./samples/Bingo.Ts/run_sample.ps1
./samples/run_samples.ps1
./samples/run_samples.ps1 Bingo.Ts SupportChat.Ts
```

## 실행 결과

개별 runner는 client self-check가 성공하면 종료 코드 `0`을 반환한다.
Bingo를 정상 실행하면 마지막에 다음 marker가 출력된다.

```text
bingo=completed
PASS Bingo.Ts
```

다른 샘플도 같은 방식으로 `PASS <Sample.Ts>` marker를 출력한다. 실패하면 runner가 실패한
프로세스와 검증 항목을 표준 오류에 출력하고 자신이 시작한 서버, Chromium과 Redis container를
정리한다.

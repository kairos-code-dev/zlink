# SupportChat 샘플

`SupportChat`은 고객 지원 대화방을 stream session, API 역할, support 역할로 나누어
구성한 .NET Framework 샘플이다. 클라이언트는 session에 연결하고, 서버는 대화 생성,
상담원 배정, 메시지 전송, 알림 push를 ZLink 메시징으로 처리한다.

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

- `Shared/`는 대화, 메시지, 알림 계약을 담는다.
- `Client/`는 고객과 상담원 흐름을 self-check로 검증한다.
- `Probe/`는 registry와 stream endpoint readiness를 확인한다.
- `Server/Api/`는 외부 요청을 받고 support 역할로 전달한다.
- `Server/Session/`은 stream session과 client push를 담당한다.
- `Server/Support/`는 대화 actor, 상담원 배정, 메시지 상태를 관리한다.
- `Server/Registry/`는 샘플 실행 중 사용할 discovery registry를 띄운다.

## 성공 조건

클라이언트 시나리오는 대화 열기, 상담원 참여, 메시지 송수신, session push를 검증한다.
`run_sample.sh`는 client log에서 `supportchat=completed`를 확인하고, 서버 evidence
확인이 끝나면 `supportchat-server-evidence=completed`를 출력한다.

# buildenv/win — Windows 개발환경 설정

Windows에서 zlink 프로젝트의 전체 개발환경을 자동으로 구축하는 PowerShell 스크립트.

## 빠른 시작

```powershell
# 현재 환경 확인 (설치 없이)
.\buildenv\win\setup.ps1

# 누락된 도구 자동 설치
.\buildenv\win\setup.ps1 -Install

# 설치 + 코어 라이브러리 빌드
.\buildenv\win\setup.ps1 -Install -BuildCore

# 개발환경 활성화
. .\buildenv\win\env.ps1
```

## 파일 구성

| 파일 | 역할 |
|------|------|
| `setup.ps1` | 개발환경 감지/설치 (메인 스크립트) |
| `env.ps1` | 환경변수 활성화 (setup.ps1이 자동 생성, gitignore 대상) |
| `vscode/settings.json` | VSCode 설정 템플릿 (IntelliSense, CMake, Java, Python) |
| `vscode/extensions.json` | VSCode 추천 확장 템플릿 |
| `vscode/tasks.json` | VSCode 빌드/테스트 태스크 템플릿 |

`setup.ps1` 실행 시 `vscode/` 템플릿을 프로젝트 루트의 `.vscode/`로 복사한다.
이미 `.vscode/` 파일이 존재하면 덮어쓰지 않는다 (개발자 로컬 커스텀 보호).

## setup.ps1 매개변수

| 매개변수 | 설명 |
|----------|------|
| (없음) | 감지 전용 — 설치 상태만 표시 |
| `-Install` | 누락된 도구를 winget으로 자동 설치 |
| `-BuildCore` | 코어 라이브러리 CMake 빌드 |
| `-NoBuildCore` | 코어 빌드 스킵 |
| `-Bindings <list>` | 특정 바인딩만 확인 (예: `java,node`) |

### 사용 예시

```powershell
# Java + Node.js 바인딩만 확인
.\buildenv\win\setup.ps1 -Bindings java,node

# 전체 설치 + 빌드
.\buildenv\win\setup.ps1 -Install -BuildCore
```

## 설치 항목

### Core 도구

| 도구 | 최소 버전 | 설치 방법 |
|------|-----------|-----------|
| Visual Studio 2022 | C++ Build Tools | winget |
| CMake | 3.10 | winget |
| Ninja | — (선택) | winget |

### 바인딩별 도구

| 바인딩 | 도구 | 최소 버전 |
|--------|------|-----------|
| .NET | .NET SDK | 8.0 |
| Java | JDK (Adoptium) | 22 |
| Java | Gradle | (wrapper) |
| Node.js | Node.js | 20.0 |
| Node.js | node-gyp | — |
| Python | Python | 3.9 |

### VSCode 설정 및 확장

`setup.ps1` 실행 시 자동으로:
1. `buildenv/win/vscode/` 템플릿을 `.vscode/`로 복사 (없는 파일만)
2. VSCode 확장 감지 및 설치 (`-Install` 시)

`.vscode/`는 gitignore 대상이므로 git에 포함되지 않는다.
VSCode가 설치되어 있지 않으면 확장 설치는 스킵한다.

## 출력 예시

```
  Core Tools
  ----------
  [OK]     VS 2022                v17.14.x @ C:\Program Files\...
  [OK]     CMake                  v3.31.6
  [??]     Ninja                  Not found (optional)
  [OK]     OpenSSL                DLLs found @ ...
  [OK]     VS Code                v1.109.0
  [OK]     Extensions             10/10 installed
```

## 참고 사항

- **멱등성**: 이미 설치된 도구, 확장, `.vscode/` 파일은 자동 스킵
- **winget 사용**: `-Install` 시 winget으로 도구 설치 (관리자 권한 불필요)
- **env.ps1**: setup.ps1 실행 시 자동 생성되며, `PATH`와 환경변수를 설정
- **.vscode/ 관리**: git에 포함하지 않고 `buildenv/win/vscode/` 템플릿에서 생성. 개발자가 로컬에서 수정한 설정은 덮어쓰지 않음

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

## 스크립트 구성

| 파일 | 역할 |
|------|------|
| `setup.ps1` | 개발환경 감지/설치 (메인 스크립트) |
| `env.ps1` | 환경변수 활성화 (setup.ps1이 자동 생성, gitignore 대상) |

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

### VSCode 확장

`.vscode/extensions.json`의 추천 확장을 자동 감지/설치한다.
VSCode가 설치되어 있지 않으면 스킵한다.

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

- **멱등성**: 이미 설치된 도구와 확장은 자동 스킵
- **winget 사용**: `-Install` 시 winget으로 도구 설치 (관리자 권한 불필요)
- **env.ps1**: setup.ps1 실행 시 자동 생성되며, `PATH`와 환경변수를 설정

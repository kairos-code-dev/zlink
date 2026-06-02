# ZLink Framework for Node.js — 개요

이 가이드는 NestJS 애플리케이션에서 ZLink Framework 를 사용하는 방법을 설명한다.
Node 버전은 .NET framework 와 같은 개념을 사용한다. 차이는 등록 표면이
`ZLinkModule.forRoot(...)`, provider token, TypeScript 타입으로 옮겨졌다는 점이다.

## 1. 무엇을 해 주는가

ZLink Framework 는 내부 서비스 통신과 실시간 상태 서버를 한 모델로 묶는다.
애플리케이션은 channel, Spot, actor, stream 을 각각 필요한 수준에서 선택한다.

| 기능 | 사용할 때 |
|------|-----------|
| channel | 서버 간 request/reply, send, publish 를 처리할 때 |
| Spot | room, stage, zone 처럼 상태를 가진 실행 단위를 만들 때 |
| actor | 사용자나 세션처럼 긴 수명을 가진 논리 객체를 다룰 때 |
| stream | 외부 클라이언트와 장기 연결을 유지할 때 |
| registry | 여러 프로세스의 topology 를 발견하고 조회할 때 |
| monitoring | runtime 상태 변화를 typed event 로 관찰할 때 |

## 2. 기준

의미와 동작은 `framework/languages/dotnet` 이 기준이다. Node 버전은 NestJS 와
TypeScript 스타일을 사용하지만, request timeout, lifecycle 순서, session actor relay,
registry query 같은 의미는 .NET 과 맞춘다.

## 3. 다음에 읽을 장

- 처음 붙이는 방법: [02-getting-started](./02-getting-started.ko.md)
- 개념 차이: [03-concepts](./03-concepts.ko.md)
- 기능 이름 매핑: [04-feature-map](./04-feature-map.ko.md)

## 회귀 테스트

이 장은 guide 12개 장 존재 여부와 링크 회귀 테스트에서 확인한다.

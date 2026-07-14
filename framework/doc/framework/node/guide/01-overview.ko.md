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
| location store | 여러 프로세스의 topology를 등록하고 자동 연결에 사용할 때 |
| monitoring | runtime 상태 변화를 typed event 로 관찰할 때 |

## 2. 기능 선택

request/reply와 단방향 메시지는 channel, 다수 구독자 이벤트는 fanout,
상태를 소유하는 실행 단위는 Spot·actor, 외부 client의 장기 연결은 stream을
사용한다. 하부 socket 배선과 자원 수명은 사용자 가이드가 아니라 internals 문서가
소유한다.

## 3. 기준

의미와 동작의 기준은 [framework 공통 spec](../../spec/README.ko.md)이다.
Node 버전은 공통 계약을 NestJS 등록 표면과 TypeScript 타입으로 표현한다.

## 4. 다음에 읽을 장

- 처음 붙이는 방법: [02-getting-started](02-getting-started.ko.md)
- 개념 차이: [03-concepts](03-concepts.ko.md)
- 기능 이름 매핑: [10-feature-map](10-feature-map.ko.md)

## 회귀 테스트

이 장은 guide 12개 장 존재 여부와 링크 회귀 테스트에서 확인한다.

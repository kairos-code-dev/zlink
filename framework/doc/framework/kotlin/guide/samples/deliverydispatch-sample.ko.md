<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:end -->

[Kotlin 묶음](../../README.ko.md) | [SPOT](../../../common/spec/languages/java/spring-boot-spot.ko.md) | [Actor/Session](../../../common/spec/languages/java/spring-boot-actor-session.ko.md) | [STREAM](../../../common/spec/languages/java/spring-boot-stream.ko.md)

# DeliveryDispatch Sample (Kotlin)

> 언어 중립 시나리오 정본은 [공통 샘플 — DeliveryDispatch](../../../common/sample/deliverydispatch/README.ko.md)다.
> Kotlin 실행 코드는 `samples/kotlin/DeliveryDispatch`에 있다.
> Kotlin 샘플은 `.NET` 기준 역할 배치와 message 계약을 따른다.
> 상세 매핑과 검증 증거는 샘플 루트의 `sample-porting-inventory.ko.md`에서 확인한다.

## 1. 목적

배송 배차, timeout 재배정, 상태 fanout, 고객 stream push를 보여 주는 샘플이다. Kotlin 구현은
`.NET`과 공통 샘플 문서의 역할, message 계약, 검증 marker를 맞추고 public framework API로 channel,
stream session, entry spot, actor 연결을 구성한다.

## 2. 서버 구성

Kotlin 샘플 루트는 `Registry`, `Dispatch`, `CourierGateway`, `CourierSession`,
`CourierSpotNode`, `Tracking`, `CustomerGateway`, `Client`, `Shared`를 별도 Gradle project로
둔다. 이 배치는 공통 문서의 server role을 분리해서 포팅하기 위한 구조다.

`run_sample.sh`는 dynamic topology를 만든 뒤 installed application으로 각 role process를 실행한다.
준비 확인은 startup log가 아니라 registry, channel, stream, HTTP endpoint가 실제로 bind됐는지
확인하는 방식으로 한다. `Dispatch`, `CourierGateway`, `CourierSession`, `CourierSpotNode`,
`CustomerGateway`, `Tracking`은 public framework API로 channel, stream session, route mesh,
entry spot, actor factory, tracking fanout을 구성한다.

## 3. 전체 흐름

완성 목표 흐름은 공통 샘플 문서를 따른다.

1. 고객이 배송 생성을 요청하면 `Dispatch`가 받아 worker 흐름에 넣는다.
2. `CourierGateway`가 courier id를 actor 위치와 session route로 해석한다.
3. `CourierSession`과 `CourierSpotNode`가 배송원 연결, actor 생성, offer 전달을 맡는다.
4. `Tracking`이 상태 event를 기록하고 `CustomerGateway`가 고객 stream으로 상태를 push한다.
5. client는 성공 배차와 timeout 재배정을 모두 검증한다.

## 4. 비동기 진행 관용구

worker와 timeout 재배정 source는 `DispatchWorker`와 별도 queue module로 분리되어 있다. client나
runner가 결과를 만들어 내지 않고, `DispatchWorker`가 courier offer timeout을 기준으로 courier-b에
재배정한다.

## 5. 경계 메모

포팅이 완료되면 Kotlin 샘플은 public framework API만 사용해야 한다. framework 내부 package,
private bridge, 테스트 전용 adapter, raw frame 우회로 누락 기능을 메우지 않는다. public API로
바로 구현할 수 없는 기능은 inventory에 gap으로 남긴 뒤 별도 설계 작업으로 분리한다.

## 6. Client self-check

client는 public stream connector wait API로 offer와 status push를 기다린다. 성공 배차는
`Assigned`, `Accepted`, `PickedUp`, `Delivered` 순서를 확인하고, 재배정 흐름은 courier-a 첫 offer 후
timeout으로 courier-b가 `Reassigned`, `Accepted`, `Delivered`를 만드는지 확인한다.

## 7. 완료 기준

- 역할 분리, fanout, 재배정 timer, 고객 push가 실제 server process 사이에서 동작한다.
- JSON codec과 public framework API를 사용한다.
- runner의 `topology=ready`, `deliverydispatch-reassignment=completed`,
  `deliverydispatch-server-evidence=completed`, `deliverydispatch=completed` marker가 실제 runtime
  증거와 연결된다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->

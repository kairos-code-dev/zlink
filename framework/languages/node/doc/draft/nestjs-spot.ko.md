<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework NestJS Registry](./nestjs-registry.ko.md) | [다음: Draft -- ZLink Framework NestJS STREAM](./nestjs-stream.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [정식 SPOT spec](../spec/nestjs-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md)

# Draft -- ZLink Framework NestJS SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 구현 기준은 [정식 SPOT spec](../spec/nestjs-spot.ko.md)이다.
> Spot factory는 문자열 이름이 아니라 Spot type 기준으로 등록한다.

## 1. 기준 표면

```ts
ZLinkModule.forRoot({
  spotMeshes: {
    'game.stage': {
      nodes: {
        'stage-node': {
          router: { bind: 'tcp://0.0.0.0:9000' },
          pubSub: { pubBind: 'tcp://0.0.0.0:9001' },
          entrySpot: GameEntrySpot,
          spotFactories: [StageSpot, RoomSpot],
        },
      },
    },
  },
});
```

Spot 생성과 조회는 type과 `spotRid`를 기준으로 한다.

```ts
const room = await spotManager.create(RoomSpot, roomRid);
const found = await spotManager.get(room.spotRid);
```

## 2. 회귀 테스트

이 draft는 아래 회귀 항목과 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| duplicate Spot factory type | Spot factory가 type key 기준으로 등록된다. |
| Registry Spot RID route | 생성된 Spot은 `spotRid` 기준으로 조회된다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework NestJS Registry](./nestjs-registry.ko.md) | [다음: Draft -- ZLink Framework NestJS STREAM](./nestjs-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->

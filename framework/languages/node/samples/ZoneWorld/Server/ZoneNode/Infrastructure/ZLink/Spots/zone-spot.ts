import { ZoneState } from '../../../Domain/zone-state';
import { Injectable, Scope } from '@nestjs/common';
import { ZoneWorldNames, ZoneWorldSpec } from '../../../../../Shared/spec';
import type { ZoneId } from '../../../../../Shared/spec';
import { ZoneBorderEvent, ZoneChangedNotify, ZoneStateNotify } from '../../../../../Shared/contracts';
import type { EnterZoneMsg } from '../../../../../Shared/contracts';
import { nodeOf } from '../../../../../Shared/spec';
import type {
  ZLinkMessage,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkTimer
} from '@zlink-systems/framework';
import type { PlayerActor } from '../Actors/player-actor';
import { PlayerActor as PlayerActorClass } from '../Actors/player-actor';
import { PlayerMoveHandler, ZoneJoinWorldHandler } from '../Handlers/player-handlers';
import { adjacentZones } from '../../../Domain/world';
import {
  FirstBorderSubscriptionHandler,
  DeliverAnnounceHandler,
  SecondBorderSubscriptionHandler,
  ZoneTickHandler
} from '../Handlers/zone-runtime-handlers';

@Injectable({ scope: Scope.TRANSIENT })
class ZoneSpot implements ZLinkSpot<PlayerActor> {
  readonly context!: ZLinkSpotContext<PlayerActor, ZoneSpot>;
  private state?: ZoneState;
  private readonly actors = new Map<string, PlayerActor>();
  private readonly pendingJoins = new Map<string, EnterZoneMsg>();
  private timer?: ZLinkTimer;

  configure(): void {
    this.context.handlers.addActorPacket(ZoneJoinWorldHandler, PlayerActorClass);
    this.context.handlers.addActorPacket(PlayerMoveHandler, PlayerActorClass);
    this.context.handlers.addPacket(DeliverAnnounceHandler);
    const adjacent = adjacentZones(String(this.context.spotRid) as ZoneId);
    this.context.handlers.addSubscribe(
      FirstBorderSubscriptionHandler,
      ZoneWorldNames.borderTopic(adjacent[0], String(this.context.spotRid))
    );
    this.context.handlers.addSubscribe(
      SecondBorderSubscriptionHandler,
      ZoneWorldNames.borderTopic(adjacent[1], String(this.context.spotRid))
    );
  }

  async onInitialize(): Promise<void> {
    this.state = new ZoneState(String(this.context.spotRid) as ZoneId);
    this.timer = await this.context.addTimer('zone-tick', ZoneWorldSpec.tickPeriodMs, ZoneTickHandler);
  }

  async onClosing(): Promise<void> {
    await this.timer?.cancel();
    this.timer = undefined;
  }

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    const enter = request.decode<EnterZoneMsg>(Object as never);
    if (enter.playerId !== actorId) {
      return { accepted: false };
    }
    console.log(`zone admission accepted zone=${String(this.context.spotRid)} player=${actorId} from=${enter.fromNodeId ?? 'new'}`);
    this.pendingJoins.set(actorId, enter);
    return { accepted: true };
  }

  async onJoinedActor(actor: PlayerActor): Promise<void> {
    const enter = this.pendingJoins.get(actor.actorId);
    if (enter === undefined) return;
    this.pendingJoins.delete(actor.actorId);
    actor.x = enter.x;
    actor.y = enter.y;
    actor.zoneId = this.requireState().zoneId;
    actor.isBot = enter.isBot;
    this.actors.set(actor.actorId, actor);
    this.requireState().enter(actor.actorId, actor.x, actor.y, actor.isBot);
    if (!actor.isBot && enter.fromNodeId !== null) {
      await actor.push(new ZoneChangedNotify(
        actor.actorId,
        actor.zoneId,
        nodeOf(actor.zoneId),
        enter.fromNodeId !== nodeOf(actor.zoneId)
      ));
    }
    console.log(`zone player entered zone=${actor.zoneId} player=${actor.actorId} from=${enter.fromNodeId ?? 'new'}`);
  }

  async onLeaveActor(actor: PlayerActor): Promise<void> {
    this.actors.delete(actor.actorId);
    this.requireState().leave(actor.actorId);
  }

  updatePosition(actor: PlayerActor): void {
    this.requireState().updatePosition(actor.actorId, actor.x, actor.y);
  }

  applyBorder(event: ZoneBorderEvent): void {
    if (event.toZoneId !== String(this.context.spotRid)) return;
    this.requireState().applyBorderSnapshot(event.fromZoneId, event.tick, event.players);
  }

  async tick(): Promise<void> {
    const state = this.requireState();
    const tick = state.nextTick();
    const visible = state.visiblePlayers();
    await Promise.allSettled(
      [...this.actors.values()]
        .filter((actor) => !actor.isBot)
        .map((actor) => actor.push(new ZoneStateNotify(state.zoneId, tick, visible)))
    );
    for (const adjacent of adjacentZones(state.zoneId)) {
      await this.context.outbound.publish(
        ZoneWorldNames.borderTopic(state.zoneId, adjacent),
        new ZoneBorderEvent(state.zoneId, adjacent, tick, state.borderBandFor(adjacent))
      ).submit();
    }
    state.expireStaleSnapshots();
  }

  async pushHumans(payload: unknown): Promise<void> {
    await Promise.allSettled(
      [...this.actors.values()].filter((actor) => !actor.isBot).map((actor) => actor.push(payload))
    );
  }

  private requireState(): ZoneState {
    if (this.state === undefined) throw new Error('Zone spot has not initialized.');
    return this.state;
  }
}

export { ZoneSpot };

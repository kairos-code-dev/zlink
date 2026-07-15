import { ZoneState } from '../../../Domain/zone-state';
import { ZoneWorldNames, ZoneWorldSpec } from '../../../../../Shared/spec';
import type { ZoneId } from '../../../../../Shared/spec';
import { ZoneBorderEvent, ZoneStateNotify } from '../../../../../Shared/contracts';
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
  SecondBorderSubscriptionHandler,
  ZoneTickHandler
} from '../Handlers/zone-runtime-handlers';

class ZoneSpot implements ZLinkSpot<PlayerActor> {
  readonly context!: ZLinkSpotContext<PlayerActor, ZoneSpot>;
  private state?: ZoneState;
  private readonly actors = new Map<string, PlayerActor>();
  private timer?: ZLinkTimer;

  configure(): void {
    this.context.handlers.addActorPacket(ZoneJoinWorldHandler, PlayerActorClass);
    this.context.handlers.addActorPacket(PlayerMoveHandler, PlayerActorClass);
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

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onJoinedActor(actor: PlayerActor): Promise<void> {
    this.actors.set(actor.actorId, actor);
    this.requireState().enter(actor.actorId, actor.x, actor.y, actor.isBot);
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

  private requireState(): ZoneState {
    if (this.state === undefined) throw new Error('Zone spot has not initialized.');
    return this.state;
  }
}

export { ZoneSpot };

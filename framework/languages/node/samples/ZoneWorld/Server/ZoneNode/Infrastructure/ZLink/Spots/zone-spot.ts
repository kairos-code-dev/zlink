import { ZoneState } from '../../../Domain/zone-state';
import { Inject, Injectable, Scope } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT, ZLINK_ACTOR_MANAGER } from '@zlink-systems/nestjs';
import { ZoneWorldNames, ZoneWorldSpec } from '../../../../../Shared/spec';
import type { ZoneId } from '../../../../../Shared/spec';
import { BotTickReq, ZoneChangedNotify, ZoneBorderEvent, ZoneStateNotify } from '../../../../../Shared/contracts';
import type { BotTickRes } from '../../../../../Shared/contracts';
import type { EnterZoneMsg } from '../../../../../Shared/contracts';
import type {
  ZLinkActorClient,
  ZLinkActorManager,
  ZLinkMessage,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkTimer
} from '@zlink-systems/framework';
import type { PlayerActor } from '../Actors/player-actor';
import { PlayerActor as PlayerActorClass } from '../Actors/player-actor';
import { PlayerBotTickHandler, PlayerMoveHandler, ZoneJoinWorldHandler } from '../Handlers/player-handlers';
import { adjacentZones } from '../../../Domain/world';
import {
  BotTickHandler,
  FirstBorderSubscriptionHandler,
  DeliverAnnounceHandler,
  SecondBorderSubscriptionHandler,
  ZoneTickHandler
} from '../Handlers/zone-runtime-handlers';
import { NodeRuntimeState } from '../../../Domain/node-runtime-state';

@Injectable({ scope: Scope.TRANSIENT })
class ZoneSpot implements ZLinkSpot<PlayerActor> {
  readonly context!: ZLinkSpotContext<PlayerActor, ZoneSpot>;
  private state?: ZoneState;
  private readonly actors = new Map<string, PlayerActor>();
  private readonly pendingJoins = new Map<string, EnterZoneMsg>();
  private timer?: ZLinkTimer;
  private botTimer?: ZLinkTimer;
  private botTickTask?: Promise<void>;
  private botTickFailure?: unknown;

  constructor(
    private readonly nodeState: NodeRuntimeState,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actorClient: ZLinkActorClient
  ) {}

  configure(): void {
    this.context.handlers.addActorPacket(ZoneJoinWorldHandler, PlayerActorClass);
    this.context.handlers.addActorPacket(PlayerMoveHandler, PlayerActorClass);
    this.context.handlers.addActorPacket(PlayerBotTickHandler, PlayerActorClass);
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
    this.timer = await this.context.addTimer(
      'zone-tick',
      ZoneWorldSpec.tickPeriodMs,
      ZoneTickHandler,
      { stopOnUnhandledException: false }
    );
    this.botTimer = await this.context.addTimer(
      'bot-tick',
      ZoneWorldSpec.botTickPeriodMs,
      BotTickHandler,
      { stopOnUnhandledException: false }
    );
  }

  async onClosing(): Promise<void> {
    await this.timer?.cancel();
    await this.botTimer?.cancel();
    this.timer = undefined;
    this.botTimer = undefined;
  }

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    const enter = request.decode<EnterZoneMsg>(Object as never);
    if (enter.playerId !== actorId) {
      return { accepted: false };
    }
    if (this.nodeState.rejectsArrival(String(this.context.spotRid), enter.fromNodeId)) {
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
    this.nodeState.joined(actor.actorId, actor.zoneId);
    this.requireState().enter(actor.actorId, actor.x, actor.y, actor.isBot);
    if (!actor.isBot && enter.fromNodeId !== null) {
      actor.push(new ZoneChangedNotify(
        actor.actorId,
        actor.zoneId,
        this.nodeState.nodeId,
        enter.fromNodeId !== this.nodeState.nodeId
      ));
    }
    console.log(`zone player entered zone=${actor.zoneId} player=${actor.actorId} from=${enter.fromNodeId ?? 'new'}`);
  }

  async onLeaveActor(actor: PlayerActor): Promise<void> {
    this.actors.delete(actor.actorId);
    this.nodeState.left(actor.actorId, actor.zoneId);
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
      this.context.outbound.publish(
        ZoneWorldNames.borderTopic(state.zoneId, adjacent),
        new ZoneBorderEvent(state.zoneId, adjacent, tick, state.borderBandFor(adjacent))
      ).submit();
    }
    for (const zoneId of state.expireStaleSnapshots()) {
      console.log(`border snapshot expired zone=${state.zoneId} source=${zoneId} tick=${tick}`);
    }
  }

  async pushHumans(payload: unknown): Promise<void> {
    await Promise.allSettled(
      [...this.actors.values()].filter((actor) => !actor.isBot).map((actor) => actor.push(payload))
    );
  }

  tickBots(): void {
    if (this.botTickFailure !== undefined) {
      const failure = this.botTickFailure;
      this.botTickFailure = undefined;
      throw failure;
    }
    if (!this.nodeState.canTickBots()) return;
    if (this.botTickTask !== undefined) return;
    const task = this.runBotTicks();
    this.botTickTask = task;
    void task.then(
      () => { if (this.botTickTask === task) this.botTickTask = undefined; },
      (error: unknown) => {
        this.botTickFailure = error;
        if (this.botTickTask === task) this.botTickTask = undefined;
      }
    );
  }

  private async runBotTicks(): Promise<void> {
    for (const actor of [...this.actors.values()].filter((candidate) => candidate.isBot)) {
      const actorRef = await this.actorManager.find(actor.actorId);
      if (actorRef !== undefined) {
        await this.actorClient.requestToActor(actorRef, new BotTickReq()).timeout(30_000).submit<BotTickRes>();
      }
    }
  }

  private requireState(): ZoneState {
    if (this.state === undefined) throw new Error('Zone spot has not initialized.');
    return this.state;
  }
}

export { ZoneSpot };

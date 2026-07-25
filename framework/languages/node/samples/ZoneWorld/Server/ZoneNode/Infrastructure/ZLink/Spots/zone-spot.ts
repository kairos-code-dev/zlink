import { ZoneState } from '../../../Domain/zone-state';
import { Inject, Injectable, Scope } from '@nestjs/common';
import {
  ZLINK_ACTOR_CLIENT,
  ZLINK_SPOT_PUBLISHER_CLIENT
} from '@zlink-systems/nestjs';
import { ZoneWorldNames, ZoneWorldSpec } from '../../../../../Shared/spec';
import type { ZoneId } from '../../../../../Shared/spec';
import { BotTickReq, ZoneChangedNotify, ZoneBorderEvent, ZoneStateNotify } from '../../../../../Shared/contracts';
import type { BotTickRes } from '../../../../../Shared/contracts';
import type { EnterZoneMsg } from '../../../../../Shared/contracts';
import type {
  ZLinkActorClient,
  ActorRef,
  ZLinkActorMembership,
  ZLinkMessage,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkSpotPublisherClient,
  ZLinkTimer
} from '@zlink-systems/framework';
import type { PlayerActor } from '../Actors/player-actor';
import { DeliverZoneNotification } from '../Actors/player-actor';
import { adjacentZones } from '../../../Domain/world';
import {
  BotTickHandler,
  FirstBorderSubscriptionHandler,
  DeliverAnnounceHandler,
  SecondBorderSubscriptionHandler,
  UpdateZonePositionHandler,
  ZoneTickHandler
} from '../Handlers/zone-runtime-handlers';
import { NodeRuntimeState } from '../../../Domain/node-runtime-state';

interface ZoneParticipant {
  readonly actor: ActorRef;
  readonly actorId: string;
  x: number;
  y: number;
  readonly zoneId: ZoneId;
  readonly isBot: boolean;
}

@Injectable({ scope: Scope.TRANSIENT })
class ZoneSpot implements ZLinkSpot<PlayerActor> {
  readonly context!: ZLinkSpotContext<PlayerActor, ZoneSpot>;
  private state?: ZoneState;
  private readonly actors = new Map<string, ZoneParticipant>();
  private readonly pendingJoins = new Map<string, EnterZoneMsg>();
  private timer?: ZLinkTimer;
  private botTimer?: ZLinkTimer;
  private botTickTask?: Promise<void>;
  private botTickFailure?: unknown;

  constructor(
    private readonly nodeState: NodeRuntimeState,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actorClient: ZLinkActorClient,
    @Inject(ZLINK_SPOT_PUBLISHER_CLIENT) private readonly publisher: ZLinkSpotPublisherClient
  ) {}

  configure(): void {
    this.context.handlers.addPacket(DeliverAnnounceHandler);
    this.context.handlers.addPacket(UpdateZonePositionHandler);
    const adjacent = adjacentZones(String(this.context.spotRid) as ZoneId);
    this.context.handlers.addSubscribe(
      FirstBorderSubscriptionHandler,
      ZoneWorldNames.bridgeMesh,
      ZoneWorldNames.borderTopic(adjacent[0], String(this.context.spotRid))
    );
    this.context.handlers.addSubscribe(
      SecondBorderSubscriptionHandler,
      ZoneWorldNames.bridgeMesh,
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

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    const actorId = actor.actor.actorId;
    const enter = this.pendingJoins.get(actorId);
    if (enter === undefined) return;
    this.pendingJoins.delete(actorId);
    const participant: ZoneParticipant = {
      actor: actor.actor,
      actorId,
      x: enter.x,
      y: enter.y,
      zoneId: this.requireState().zoneId,
      isBot: enter.isBot
    };
    this.actors.set(actorId, participant);
    this.nodeState.joined(actorId, participant.zoneId);
    this.requireState().enter(actorId, participant.x, participant.y, participant.isBot);
    if (!participant.isBot && enter.fromNodeId !== null) {
      await this.notifyActor(participant.actor, new ZoneChangedNotify(
        actorId,
        participant.zoneId,
        this.nodeState.nodeId,
        enter.fromNodeId !== this.nodeState.nodeId
      ));
    }
    console.log(`zone player entered zone=${participant.zoneId} player=${actorId} from=${enter.fromNodeId ?? 'new'}`);
  }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> {
    const participant = this.actors.get(actor.actor.actorId);
    this.actors.delete(actor.actor.actorId);
    if (participant !== undefined) this.nodeState.left(participant.actorId, participant.zoneId);
    this.requireState().leave(actor.actor.actorId);
  }

  async onDisconnectActor(_actor: ZLinkActorMembership): Promise<void> {}

  updatePosition(actorId: string, x: number, y: number): void {
    const actor = this.actors.get(actorId);
    if (actor === undefined) throw new Error(`Player '${actorId}' is not joined to this zone.`);
    actor.x = x;
    actor.y = y;
    this.requireState().updatePosition(actorId, x, y);
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
        .map((actor) => this.notifyActor(actor.actor, new ZoneStateNotify(state.zoneId, tick, visible)))
    );
    for (const adjacent of adjacentZones(state.zoneId)) {
      await this.publisher.publish(
        ZoneWorldNames.zoneMesh,
        ZoneWorldNames.bridgeMesh,
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
      [...this.actors.values()].filter((actor) => !actor.isBot).map((actor) => this.notifyActor(actor.actor, payload))
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
      await this.actorClient.requestToActor(
        ZoneWorldNames.zoneMesh,
        actor.actor,
        new BotTickReq()
      ).submit<BotTickRes>();
    }
  }

  private async notifyActor(actor: ActorRef, payload: unknown): Promise<void> {
    await this.actorClient
      .sendToActor(ZoneWorldNames.zoneMesh, actor, new DeliverZoneNotification(payload))
      .submit();
  }

  private requireState(): ZoneState {
    if (this.state === undefined) throw new Error('Zone spot has not initialized.');
    return this.state;
  }
}

export { ZoneSpot };

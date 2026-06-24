import { Injectable } from '@nestjs/common';
import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_GATEWAY } from '@zlink-systems/nestjs';
import { PlayActorJoinGameHandler } from './Handlers/play-actor-join-game-handler';
import { PacketNames, observeMilestoneRes, winMilestoneNotify } from '../../../../../Shared/Contracts/messages';
import { PlayerWinMilestoneEventHandler } from './Handlers/player-win-milestone-event-handler';
import { SampleNames } from '../../../../Configuration/sample-settings';
import type {
  ActorRef,
  ZLinkActorGateway,
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage
} from '@zlink-systems/framework';
import type {
  JoinGameRes,
  PlayerInfo,
  PlayerWinMilestoneEvent,
  TicTacToeGameJoinReq,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

type PlayEntrySpotActor = TicTacToeActor & ZLinkActor;

@Injectable()
class MilestoneObserverRegistry {
  private readonly observers = new Map<string, PlayEntrySpotActor>();

  subscribe(actor: PlayEntrySpotActor): void {
    this.observers.set(actor.actorId, actor);
  }

  remove(actor: PlayEntrySpotActor): void {
    this.observers.delete(actor.actorId);
  }

  async notify(event: PlayerWinMilestoneEvent, receivingSpotNodeRid: string): Promise<void> {
    const payload = winMilestoneNotify(event, receivingSpotNodeRid);
    for (const actor of [...this.observers.values()]) {
      await actor.push(PacketNames.winMilestoneNotify, payload);
    }
  }
}

@Injectable()
class PlayEntrySpot implements ZLinkEntrySpot<PlayEntrySpotActor> {
  readonly context!: ZLinkEntrySpotContext<PlayEntrySpotActor>;

  constructor(
    private readonly milestoneObservers: MilestoneObserverRegistry,
    @Inject(ZLINK_ACTOR_GATEWAY) private readonly actorGateway: ZLinkActorGateway
  ) {}

  configure(): void {
    this.context.handlers.actorRequest(PacketNames.joinGameReq, PlayActorJoinGameHandler);
    this.context.handlers.addSubscribe(PlayerWinMilestoneEventHandler, SampleNames.playerMilestoneTopic);
  }

  async join(actorRef: ActorRef, player: PlayerInfo, roomId: string): Promise<JoinGameRes> {
    const request: TicTacToeGameJoinReq = {
      roomId,
      player: {
        actorId: player.actorId,
        displayName: player.displayName,
        level: player.level,
        wins: player.wins
      }
    };
    const joined = await this.actorGateway.joinSpot(actorRef, roomId, request).submit<Partial<JoinGameRes & { error: string }>>();
    const reply = joined.reply ?? {};
    if (joined.resultCode !== 0) {
      throw new Error(reply.error ?? `Room '${roomId}' rejected actor '${actorRef.actorId}'.`);
    }
    return reply as JoinGameRes;
  }

  async observeMilestone(actor: PlayEntrySpotActor): Promise<unknown> {
    this.milestoneObservers.subscribe(actor);
    return observeMilestoneRes(true);
  }

  async notifyMilestone(event: PlayerWinMilestoneEvent): Promise<void> {
    await this.milestoneObservers.notify(event, String(this.context.nodeRid));
  }

  async onDisconnectActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    void signal;
    actor.markDisconnected();
    this.milestoneObservers.remove(actor);
  }

  async onCreateActor(actor: PlayEntrySpotActor, createRequest: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    void signal;
    const player = createRequest.decode<Partial<TicTacToeActor>>(Object as never);
    if (typeof player.displayName === 'string') {
      actor.displayName = player.displayName;
    }
    if (typeof player.level === 'number') {
      actor.level = player.level;
    }
    if (typeof player.wins === 'number') {
      actor.wins = player.wins;
    }
  }

  async onJoinedActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    if (actor.destroyAfterEntrySpotJoin) {
      await this.context.destroyActor(actor, signal);
    }
  }

  async onLeaveActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    void signal;
    console.log(`entry spot: actor left. actor=${actor.actorId}`);
    this.milestoneObservers.remove(actor);
  }

}

Inject(MilestoneObserverRegistry)(PlayEntrySpot, undefined, 0);
Inject(ZLINK_ACTOR_GATEWAY)(PlayEntrySpot, undefined, 1);

export { MilestoneObserverRegistry, PlayEntrySpot };

import { Inject, Injectable } from '@nestjs/common';
import { PlayActorJoinGameHandler } from './Handlers/play-actor-join-game-handler';
import { PlayActorObserveMilestoneHandler } from './Handlers/play-actor-observe-milestone-handler';
import { PlayActor } from '../../Actors/play-actor';
import { joinGameRes, observeMilestoneRes, winMilestoneNotify } from '../../../../../../Shared/Contracts/messages';
import { PlayerWinMilestoneEventHandler } from './Handlers/player-win-milestone-event-handler';
import { SampleNames } from '../../../../../Configuration/sample-settings';
import type {
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage
} from '@zlink-systems/framework';
import type {
  JoinGameRes,
  ObserveMilestoneRes,
  PlayerInfo,
  PlayerWinMilestoneEvent,
  TicTacToeGameJoinReq,
  TicTacToeGameJoinRes,
  TicTacToeActor
} from '../../../../../../Shared/Contracts/messages';

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
      await actor.push(payload);
    }
  }
}

@Injectable()
class PlayEntrySpot implements ZLinkEntrySpot<PlayEntrySpotActor> {
  readonly context!: ZLinkEntrySpotContext<PlayEntrySpotActor>;

  constructor(
    private readonly milestoneObservers: MilestoneObserverRegistry
  ) {}

  configure(): void {
    this.context.handlers.addActorPacket(PlayActorJoinGameHandler, PlayActor);
    this.context.handlers.addActorPacket(PlayActorObserveMilestoneHandler, PlayActor);
    this.context.handlers.addSubscribe(PlayerWinMilestoneEventHandler, SampleNames.playerMilestoneTopic);
  }

  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }

  async join(actor: PlayEntrySpotActor, player: PlayerInfo, roomId: string): Promise<JoinGameRes> {
    const request: TicTacToeGameJoinReq = {
      roomId,
      player: {
        actorId: player.actorId,
        displayName: player.displayName,
        level: player.level,
        wins: player.wins
      }
    };
    const joined = await actor.context.joinSpot(roomId, request).submit<Partial<TicTacToeGameJoinRes & { error: string }>>();
    const reply = joined.reply;
    if (joined.status === 'rejected') {
      throw new Error(reply.error ?? `Room '${roomId}' rejected actor '${actor.actorId}'.`);
    }
    if (reply.state === undefined) {
      throw new Error(`Room '${roomId}' accepted actor '${actor.actorId}' without game state.`);
    }
    return joinGameRes(reply.state);
  }

  async observeMilestone(actor: PlayEntrySpotActor): Promise<ObserveMilestoneRes> {
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
      console.log(`entry spot: actor destroy started. actor=${actor.actorId}`);
      await this.context.destroyActor(actor, signal);
      console.log(`entry spot: actor destroyed. actor=${actor.actorId}`);
    }
  }

  async onLeaveActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    void signal;
    console.log(`entry spot: actor left. actor=${actor.actorId}`);
    this.milestoneObservers.remove(actor);
  }

}

Inject(MilestoneObserverRegistry)(PlayEntrySpot, undefined, 0);

export { MilestoneObserverRegistry, PlayEntrySpot };

import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import {
  BingoRewardItems
} from '../../../../../../Shared/Contracts/messages';
import {
  BingoGameEndedNotify,
  BingoGameStartedNotify,
  BingoNumberDrawnNotify,
  BingoRewardAcquiredEvent,
  BingoRewardAnnouncedNotify,
  BingoRoomJoinRes,
  GetPlayerRecordReq,
  ReportBingoResultReq,
  PlayerJoinedNotify,
  SubmitBingoCardRes
} from '../../../../../../Shared/Contracts/bingo-messages.generated';
import { BingoRoomGame } from '../../../../Domain/Bingo/bingo-room-game';
import { BingoRoomStatus } from '../../../../Domain/Bingo/bingo-room-game';
import { createRoomSettings, roomSettingsFromPayload } from '../../../../Domain/Bingo/bingo-room-models';
import { SampleNames } from '../../../../../Configuration/sample-names';
import { BingoRoomTimerHandler } from './Handlers/bingo-room-timer-handler';
import type {
  ZLinkMessage,
  ZLinkChannelClient,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkSpotCreateResponse,
  ZLinkTimer
} from '@zlink-systems/framework';
import type { PlayerActor as PlayerActorType } from '../../Actors/player-actor';
import type {
  BingoRoomGame as BingoRoomGameType,
  BingoRoomSnapshot
} from '../../../../Domain/Bingo/bingo-room-game';
import type { BingoRoomSettings as BingoRoomRuntimeSettings } from '../../../../Domain/Bingo/bingo-room-models';
import type {
  BingoRoomJoinReq,
  GetPlayerRecordRes,
  ReportBingoResultRes,
  SubmitBingoCardReq,
  StopObservingBingoEventsReq
} from '../../../../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
  displayName: string;
};

@Injectable()
class BingoRoomSpot implements ZLinkSpot<PlayerActorType> {
  readonly context!: ZLinkSpotContext<PlayerActorType, BingoRoomSpot>;
  private roomId: string;
  private game: BingoRoomGameType;
  private settings: BingoRoomRuntimeSettings;
  private readonly observerActors = new Map<string, PlayerActorType>();
  private readonly pendingJoins = new Map<string, BingoRoomJoinReq>();
  private readonly pendingPlayerJoins = new Map<string, {
    readonly joined: boolean;
    readonly seat: number;
    readonly isHost: boolean;
    readonly started: boolean;
  }>();
  private drawTimer?: ZLinkTimer;
  private cleanupStarted = false;

  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {
    this.roomId = 'bingo-room';
    this.settings = createRoomSettings(0);
    this.game = new BingoRoomGame(this.roomId, this.settings);
  }

  async onCreate(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse> {
    this.roomId = this.context.spotRid;
    const settings = request.decode<unknown>(Object as never);
    if (settings !== undefined) {
      this.initializeRoom(roomSettingsFromPayload(settings));
    }
    return { accepted: true };
  }

  async onInitialize(): Promise<void> {
    this.roomId = this.context.spotRid;
  }

  async onClosing(): Promise<void> {
    await this.drawTimer?.cancel();
    this.drawTimer = undefined;
  }

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    try {
      const joinRequest = request.decode<BingoRoomJoinReq>(Object as never);
      const joined = this.admitActor(actorId, joinRequest);
      return {
        accepted: true,
        reply: joined
      };
    } catch {
      return {
        accepted: false
      };
    }
  }

  admitActor(actorId: string, request: BingoRoomJoinReq): BingoRoomJoinRes {
    if (actorId !== request.actorId) {
      throw new Error('Join request actor id does not match bound actor.');
    }
    this.pendingJoins.set(actorId, request);
    if (request.observeOnly) {
      return this.admitObserver(request);
    }
    if (this.isObserverRoom()) {
      throw new Error('Player actor cannot join an observer BingoRoom.');
    }
    if (request.roomId !== this.roomId) {
      throw new Error(`Join request room id '${request.roomId}' does not match '${this.roomId}'.`);
    }
    const joined = this.game.join({ actorId, displayName: request.displayName });
    const state = this.snapshot();
    this.pendingPlayerJoins.set(actorId, {
      joined: joined.joined,
      seat: joined.player.seat,
      isHost: joined.player.isHost,
      started: joined.started
    });
    return new BingoRoomJoinRes({ state });
  }

  initializeRoom(settings: BingoRoomRuntimeSettings): void {
    this.settings = settings;
    this.game = new BingoRoomGame(this.roomId, settings);
    this.cleanupStarted = false;
  }

  async onJoinedActor(actor: PlayerActorType): Promise<void> {
    const request = this.pendingJoins.get(actor.actorId);
    if (request === undefined) {
      return;
    }
    this.pendingJoins.delete(actor.actorId);
    actor.displayName = request.displayName;
    if (request.observeOnly) {
      this.observerActors.set(actor.actorId, actor);
      console.error(`bingo observer joined spot=${this.context.spotRid} actor=${actor.actorId} room=${request.roomId}`);
      return;
    }
    const joined = this.pendingPlayerJoins.get(actor.actorId);
    this.pendingPlayerJoins.delete(actor.actorId);
    const player = this.game.players.find((candidate) => candidate.actor.actorId === actor.actorId);
    if (player === undefined || joined === undefined) {
      throw new Error(`Accepted Bingo actor '${actor.actorId}' has no pending room membership.`);
    }
    player.actor = actor;
    const record = await this.channels
      .requestToChannel(SampleNames.apiChannel, new GetPlayerRecordReq({ actorId: actor.actorId }))
      .submit<GetPlayerRecordRes>();
    this.game.setPlayerRecord(actor.actorId, record.wins, record.losses);
    console.error(`bingo-record fetched actor=${actor.actorId} wins=${record.wins} losses=${record.losses}`);
    const state = this.snapshot();
    if (joined.joined) {
      await this.notifyPlayerJoined(actor, joined.seat, joined.isHost, state);
    }
    if (joined.started) {
      await this.notifyGameStarted();
    }
  }

  async onLeaveActor(actor: PlayerActorType): Promise<void> {
    if (this.observerActors.delete(actor.actorId)) {
      console.error(`bingo-lifecycle room-leave actor=${actor.actorId} spot=${this.context.spotRid}`);
      return;
    }
    const state = this.snapshot();
    const record = await this.channels
      .requestToChannel(SampleNames.apiChannel, new ReportBingoResultReq({
        roomId: this.roomId,
        actorId: actor.actorId,
        won: state.winners.includes(actor.actorId),
        finalDrawSeq: state.drawSeq
      }))
      .submit<ReportBingoResultRes>();
    console.error(`bingo-record reported actor=${actor.actorId} wins=${record.wins} losses=${record.losses}`);
    console.error(`bingo-lifecycle room-leave actor=${actor.actorId} spot=${this.context.spotRid}`);
  }

  async onDisconnectActor(actor: PlayerActorType): Promise<void> {
    actor.markDisconnected();
  }

  async submitCard(actor: PlayerActorType | BingoActor, request: SubmitBingoCardReq): Promise<SubmitBingoCardRes> {
    this.game.submitCard(actor.actorId, request.card);
    if (this.drawTimer === undefined && this.game.canDraw()) {
      this.drawTimer = await this.context.addTimer('bingo-draw', 200, BingoRoomTimerHandler);
      console.error(`bingo-lifecycle timer-started spot=${this.context.spotRid}`);
    }
    return new SubmitBingoCardRes({ state: this.snapshot() });
  }

  async drawNextNumber(): Promise<BingoRoomSnapshot> {
    const drawn = this.game.drawNext();
    if (drawn === null) {
      return this.snapshot();
    }
    const state = this.snapshot();
    await this.pushPlayers(
      this.playerActors(),
      new BingoNumberDrawnNotify({ roomId: this.roomId, drawSeq: drawn.drawSeq, number: drawn.number, state })
    );
    if (drawn.finished) {
      await this.drawTimer?.cancel();
      this.drawTimer = undefined;
      await this.pushPlayers(
        this.playerActors(),
        new BingoGameEndedNotify({ state })
      );
      await this.publishReward(state);
      await this.leaveFinishedActors();
    }
    return state;
  }

  async announceReward(event: BingoRewardAcquiredEvent): Promise<void> {
    if (
      !this.isObserverRoom() ||
      this.settings.observedRoomId !== event.roomId ||
      this.observerActors.size === 0
    ) {
      console.error(
        `bingo reward ignored spot=${this.context.spotRid} observer=${this.isObserverRoom()} observed=${this.settings.observedRoomId ?? '-'} event=${event.roomId} observers=${this.observerActors.size}`
      );
      return;
    }
    console.error(`bingo reward announcing spot=${this.context.spotRid} observers=${this.observerActors.size}`);
    await Promise.all([...this.observerActors.values()].map((observer) =>
      observer.push(
        new BingoRewardAnnouncedNotify({ ...event, receivingSpotNodeRid: String(this.context.nodeRid) })
      )
    ));
  }

  async stopObserving(actor: PlayerActorType, request: StopObservingBingoEventsReq): Promise<boolean> {
    if (
      !this.isObserverRoom() ||
      this.settings.observedRoomId !== request.roomId ||
      !this.observerActors.has(actor.actorId)
    ) {
      return false;
    }
    this.observerActors.delete(actor.actorId);
    await this.context.leaveActor(actor);
    return true;
  }

  private async leaveFinishedActors(): Promise<void> {
    if (this.cleanupStarted || this.snapshot().status !== BingoRoomStatus.Finished) {
      return;
    }
    this.cleanupStarted = true;
    for (const player of [...this.game.players]) {
      const actor = player.actor as PlayerActorType;
      actor.markForDestroyAfterRoomLeave();
      await this.context.leaveActor(actor);
    }
    await this.context.close().catch((error) => {
      console.error(`bingo-lifecycle room-close-failed spot=${this.context.spotRid}`, error);
    });
  }

  snapshot(): BingoRoomSnapshot {
    return this.game.snapshot();
  }

  private playerActors(): PlayerActorType[] {
    return this.game.players.map((player) => player.actor as PlayerActorType);
  }

  private async pushPlayers(players: PlayerActorType[], payload: unknown): Promise<void> {
    await Promise.all(players.map((player) => player.push(payload)));
  }

  private async notifyPlayerJoined(
    actor: PlayerActorType,
    seat: number,
    isHost: boolean,
    state: BingoRoomSnapshot
  ): Promise<void> {
    await this.pushPlayers(
      this.playerActors().filter((entry) => entry.actorId !== actor.actorId),
      new PlayerJoinedNotify({
        roomId: this.roomId,
        actorId: actor.actorId,
        displayName: actor.displayName,
        seat,
        isHost,
        state
      })
    );
  }

  private async notifyGameStarted(): Promise<void> {
    await this.pushPlayers(
      this.playerActors(),
      new BingoGameStartedNotify({ state: this.snapshot() })
    );
  }

  private admitObserver(request: BingoRoomJoinReq): BingoRoomJoinRes {
    if (!this.isObserverRoom() || this.settings.observedRoomId !== request.roomId) {
      throw new Error('Observe-only actor can join only its observer BingoRoom.');
    }
    return new BingoRoomJoinRes({ state: {
        roomId: request.roomId,
        status: BingoRoomStatus.Running,
        hostActorId: '',
        canStart: false,
        drawSeq: 0,
        lastDrawnNumber: null,
        drawnNumbers: [],
        players: [],
        winners: []
      } });
  }

  private async publishReward(state: BingoRoomSnapshot): Promise<void> {
    const winner = state.winners.at(0);
    if (winner === undefined) {
      return;
    }
    await this.context.outbound
      .publish(
        SampleNames.roomRewardTopic,
        new BingoRewardAcquiredEvent({
          roomId: state.roomId,
          actorId: winner,
          drawSeq: state.drawSeq,
          itemId: BingoRewardItems.goldenDauberId,
          itemName: BingoRewardItems.goldenDauberName,
          rarity: BingoRewardItems.legendaryRarity
        })
      )
      .submit();
  }

  private isObserverRoom(): boolean {
    return this.settings.purpose === 'Observer';
  }
}

export { BingoRoomSpot };
export type { BingoActor };

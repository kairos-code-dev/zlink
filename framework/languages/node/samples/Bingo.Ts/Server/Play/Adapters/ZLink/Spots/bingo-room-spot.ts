import { Injectable } from '@nestjs/common';
import { createProtobufMessageSerializer } from '@zlink-systems/framework-codec-protobuf';
import {
  BingoRewardItems,
  BingoRoomStatus,
  PacketNames,
  bingoRewardAcquiredEvent,
  bingoRewardAnnouncedNotify,
  numberDrawnNotify,
  playerJoinedNotify,
  stateEnvelope,
  submitBingoCardRes
} from '../../../../../Shared/Contracts/messages';
import { BingoRoomGame } from '../../../Domain/Bingo/bingo-room-game';
import { createRoomSettings, roomSettingsFromPayload } from '../../../Domain/Bingo/bingo-room-models';
import { SampleNames } from '../../../../Configuration/sample-names';
import { SubmitBingoCardHandler } from './Handlers/submit-bingo-card-handler';
import { StopObservingBingoEventsHandler } from './Handlers/stop-observing-bingo-events-handler';
import { BingoRoomTimerHandler } from './Handlers/bingo-room-timer-handler';
import { BingoRewardAcquiredEventHandler } from './Handlers/bingo-reward-acquired-event-handler';
import type {
  Message,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkSpotCreateResponse,
  ZLinkTimer
} from '@zlink-systems/framework';
import type { PlayerActor as PlayerActorType } from '../Actors/player-actor';
import type {
  BingoRoomGame as BingoRoomGameType,
  BingoRoomSnapshot
} from '../../../Domain/Bingo/bingo-room-game';
import type { BingoRoomSettings as BingoRoomRuntimeSettings } from '../../../Domain/Bingo/bingo-room-models';
import type {
  BingoRewardAcquiredEvent,
  BingoRoomJoinReq,
  BingoRoomJoinRes,
  SubmitBingoCardReq,
  SubmitBingoCardRes,
  StopObservingBingoEventsReq
} from '../../../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
  displayName: string;
};

const protobufSerializer = createProtobufMessageSerializer();

@Injectable()
class BingoRoomSpot implements ZLinkSpot<PlayerActorType> {
  readonly context!: ZLinkSpotContext<PlayerActorType, BingoRoomSpot>;
  private roomId: string;
  private game: BingoRoomGameType;
  private settings: BingoRoomRuntimeSettings;
  private readonly observerActors = new Map<string, PlayerActorType>();
  private drawTimer?: ZLinkTimer;
  private cleanupStarted = false;

  constructor() {
    this.roomId = 'bingo-room';
    this.settings = createRoomSettings(0);
    this.game = new BingoRoomGame(this.roomId, this.settings);
  }

  configure(): void {
    this.context.handlers.actorRequest(PacketNames.submitBingoCardReq, SubmitBingoCardHandler);
    this.context.handlers.actorRequest(PacketNames.stopObservingBingoEventsReq, StopObservingBingoEventsHandler);
    this.context.handlers.addSubscribe(BingoRewardAcquiredEventHandler, SampleNames.roomRewardTopic);
  }

  async onCreate(request: Message): Promise<ZLinkSpotCreateResponse> {
    if (request.size() > 0) {
      this.initializeRoom(roomSettingsFromPayload(
        protobufSerializer.deserialize(request, Object as never)
      ));
    }
    return { accepted: true };
  }

  async onInitialize(): Promise<void> {
    this.roomId = this.context.spotRid;
    if (!this.isObserverRoom()) {
      this.drawTimer = await this.context.addTimer('bingo-draw', 200, BingoRoomTimerHandler);
    }
  }

  async onClosing(): Promise<void> {
    await this.drawTimer?.cancel();
    this.drawTimer = undefined;
  }

  async onActorJoin(actor: PlayerActorType, request: Message): Promise<ZLinkSpotActorJoinResponse> {
    try {
      const joinRequest = protobufSerializer.deserialize<BingoRoomJoinReq>(request, Object as never);
      const joined = await this.joinActor(actor, joinRequest);
      return {
        accepted: true,
        reply: protobufSerializer.serialize(joined)
      };
    } catch (error) {
      return {
        accepted: false
      };
    }
  }

  async joinActor(actor: PlayerActorType, request: BingoRoomJoinReq): Promise<BingoRoomJoinRes> {
    if (actor.actorId !== request.actorId) {
      throw new Error('Join request actor id does not match bound actor.');
    }
    actor.displayName = request.displayName;
    if (request.observeOnly) {
      return this.joinObserver(actor, request);
    }
    if (this.isObserverRoom()) {
      throw new Error('Player actor cannot join an observer BingoRoom.');
    }
    if (request.roomId !== this.roomId) {
      throw new Error(`Join request room id '${request.roomId}' does not match '${this.roomId}'.`);
    }
    const joined = this.game.join(actor);
      const state = this.snapshot();
      if (joined.joined) {
        await this.pushPlayers(
          this.game.players
            .map((entry) => entry.actor as PlayerActorType)
            .filter((entry) => entry.actorId !== actor.actorId),
          'PlayerJoinedNotify',
          playerJoinedNotify(this.roomId, actor, joined.player.seat, joined.player.isHost, state)
        );
      }
      if (joined.started) {
        await this.pushPlayers(
          this.playerActors(),
          'BingoGameStartedNotify',
          stateEnvelope(this.snapshot())
        );
      }
    return { state };
  }

  initializeRoom(settings: BingoRoomRuntimeSettings): void {
    this.settings = settings;
    this.game = new BingoRoomGame(this.roomId, settings);
    this.cleanupStarted = false;
  }

  async onJoinedActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: PlayerActorType): Promise<void> {
    this.observerActors.delete(actor.actorId);
  }

  async onDisconnectActor(actor: PlayerActorType): Promise<void> {
    actor.markDisconnected();
  }

  async submitCard(actor: PlayerActorType | BingoActor, request: SubmitBingoCardReq): Promise<SubmitBingoCardRes> {
    this.game.submitCard(actor.actorId, request.card);
    return submitBingoCardRes(this.snapshot());
  }

  async drawNextNumber(): Promise<BingoRoomSnapshot> {
    const drawn = this.game.drawNext();
    if (drawn === null) {
      return this.snapshot();
    }
    const state = this.snapshot();
    await this.pushPlayers(
      this.playerActors(),
      'BingoNumberDrawnNotify',
      numberDrawnNotify(this.roomId, drawn.drawSeq, drawn.number, state)
    );
    if (drawn.finished) {
      await this.pushPlayers(
        this.playerActors(),
        'BingoGameEndedNotify',
        stateEnvelope(state)
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
      return;
    }
    for (const observer of [...this.observerActors.values()]) {
      await observer.push(
        PacketNames.rewardAnnouncedNotify,
        bingoRewardAnnouncedNotify(event, String(this.context.nodeRid))
      );
    }
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
  }

  snapshot(): BingoRoomSnapshot {
    return this.game.snapshot();
  }

  private playerActors(): PlayerActorType[] {
    return this.game.players.map((player) => player.actor as PlayerActorType);
  }

  private async pushPlayers(players: PlayerActorType[], packetName: string, payload: unknown): Promise<void> {
    await Promise.all(players.map((player) => player.push(packetName, payload)));
  }

  private joinObserver(actor: PlayerActorType, request: BingoRoomJoinReq): BingoRoomJoinRes {
    if (!this.isObserverRoom() || this.settings.observedRoomId !== request.roomId) {
      throw new Error('Observe-only actor can join only its observer BingoRoom.');
    }
    this.observerActors.set(actor.actorId, actor);
    return {
      state: {
        roomId: request.roomId,
        status: BingoRoomStatus.Running,
        hostActorId: null,
        canStart: false,
        drawSeq: 0,
        lastDrawnNumber: null,
        drawnNumbers: [],
        players: [],
        winners: []
      }
    };
  }

  private async publishReward(state: BingoRoomSnapshot): Promise<void> {
    const winner = state.winners[0];
    if (winner === undefined) {
      return;
    }
    await this.context.outbound
      .publish(
        SampleNames.roomRewardTopic,
        bingoRewardAcquiredEvent({
          roomId: state.roomId,
          actorId: winner,
          drawSeq: state.drawSeq,
          itemId: BingoRewardItems.goldenDauberId,
          itemName: BingoRewardItems.goldenDauberName,
          rarity: BingoRewardItems.legendaryRarity
        })
      )
      .packetName(PacketNames.bingoRewardAcquiredEvent)
      .submit();
  }

  private isObserverRoom(): boolean {
    return this.settings.purpose === 'Observer';
  }
}

export { BingoRoomSpot };
export type { BingoActor };

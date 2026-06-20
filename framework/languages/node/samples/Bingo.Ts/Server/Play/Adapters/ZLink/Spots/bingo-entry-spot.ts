import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { createProtobufMessageSerializer } from '@zlink-systems/framework-codec-protobuf';
import {
  bingoRoomJoinReq,
  bingoRoomSettingsPayload,
  matchBingoRes,
  observeBingoEventsRes
} from '../../../../../Shared/Contracts/messages';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import { createObserverRoomSettings } from '../../../Domain/Bingo/bingo-room-models';
import { BingoRoomSpot } from './bingo-room-spot';
import { MatchBingoActorHandler } from './Handlers/match-bingo-actor-handler';
import { ObserveBingoEventsHandler } from './Handlers/observe-bingo-events-handler';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  BingoRoomJoinRes,
  MatchBingoReq,
  MatchBingoRes,
  ObserveBingoEventsReq,
  ObserveBingoEventsRes
} from '../../../../../Shared/Contracts/messages';
import type { PlayerActor as PlayerActorType } from '../Actors/player-actor';

const protobufSerializer = createProtobufMessageSerializer();

class BingoEntrySpot implements ZLinkEntrySpot<PlayerActorType> {
  readonly context!: ZLinkEntrySpotContext;

  constructor(
    private readonly roomDirectory: BingoRoomAllocatorType,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  configure(): void {
    this.context.handlers.actorRequest(PacketNames.matchBingoReq, MatchBingoActorHandler);
    this.context.handlers.actorRequest(PacketNames.observeBingoEventsReq, ObserveBingoEventsHandler);
  }

  async matchActor(actor: PlayerActorType, request: MatchBingoReq): Promise<MatchBingoRes> {
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-entry-match allocate actor=${actor.actorId}`);
    }
    const allocated = await this.roomDirectory.allocate(actor, request.mode);
    const roomId = allocated.roomId;
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-entry-match join actor=${actor.actorId} room=${roomId}`);
    }
    const joined = await actor.context
      .joinSpot(roomId, bingoRoomJoinReq(roomId, actor.actorId, actor.displayName))
      .submit<BingoRoomJoinRes>();
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-entry-match joined actor=${actor.actorId} room=${roomId} result=${joined.resultCode}`);
    }
    if (joined.resultCode !== 0) {
      throw new Error(`Room ${roomId} rejected actor '${actor.actorId}'.`);
    }
    const state = joined.reply?.state ?? await this.roomDirectory.executeInRoom(roomId, (room) => room.snapshot());
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-entry-match done actor=${actor.actorId} room=${roomId}`);
    }
    return matchBingoRes(roomId, state, allocated.ownerPlayNodeRid);
  }

  async observeEvents(actor: PlayerActorType, request: ObserveBingoEventsReq): Promise<ObserveBingoEventsRes> {
    const observerRid = this.observerRoomRid(request.roomId);
    const settings = createObserverRoomSettings(request.roomId, String(this.context.nodeRid));
    const created = await this.spots.getOrCreate(
      BingoRoomSpot,
      observerRid,
      protobufSerializer.serialize(bingoRoomSettingsPayload(settings))
    );
    if (created.state === 'rejected') {
      throw new Error('Observer BingoRoom creation was rejected.');
    }
    const joined = await actor.context
      .joinSpot(observerRid, bingoRoomJoinReq(request.roomId, actor.actorId, actor.displayName, true))
      .submit<BingoRoomJoinRes>();
    return observeBingoEventsRes(joined.resultCode === 0, String(this.context.nodeRid));
  }

  private observerRoomRid(roomId: string): string {
    return `observe:${roomId}:${String(this.context.nodeRid)}`;
  }

  async onJoinedActor(actor: PlayerActorType): Promise<void> {
    if (!actor.destroyAfterEntrySpotJoin) {
      return;
    }
    await this.context.destroyActor(actor);
  }

  async onCreateActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onDisconnectActor(actor: PlayerActorType): Promise<void> {
    actor.markDisconnected();
  }
}

Inject(BingoRoomAllocator)(BingoEntrySpot, undefined, 0);
Inject(ZLINK_SPOT_MANAGER)(BingoEntrySpot, undefined, 1);

export { BingoEntrySpot };

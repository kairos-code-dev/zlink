import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import {
  bingoRoomJoinReq,
  bingoRoomSettingsPayload,
  observeBingoEventsRes
} from '../../../../../../Shared/Contracts/messages';
import { createObserverRoomSettings } from '../../../../Domain/Bingo/bingo-room-models';
import { BingoRoomSpot } from '../BingoRoomSpot/bingo-room-spot';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type {
  BingoRoomJoinRes,
  EnsurePlayerActorReq,
  ObserveBingoEventsReq,
  ObserveBingoEventsRes
} from '../../../../../../Shared/Contracts/messages';
import type { PlayerActor as PlayerActorType } from '../../Actors/player-actor';

class BingoEntrySpot implements ZLinkEntrySpot<PlayerActorType> {
  readonly context!: ZLinkEntrySpotContext;

  constructor(
    private readonly spots: ZLinkSpotManager
  ) {}

  async observeEvents(actor: PlayerActorType, request: ObserveBingoEventsReq): Promise<ObserveBingoEventsRes> {
    const observerRid = this.observerRoomRid(request.roomId);
    const settings = createObserverRoomSettings(request.roomId, String(this.context.nodeRid));
    await this.spots.getOrCreate(
      BingoRoomSpot,
      observerRid,
      bingoRoomSettingsPayload(settings)
    );
    const joined = await actor.context
      .joinSpot(observerRid, bingoRoomJoinReq(request.roomId, actor.actorId, actor.displayName, true))
      .submit<BingoRoomJoinRes>();
    return observeBingoEventsRes(joined.accepted, String(this.context.nodeRid));
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

  async onCreateActor(actor: PlayerActorType, createRequest: ZLinkMessage): Promise<void> {
    const request = createRequest.decode<Partial<EnsurePlayerActorReq>>(Object as never);
    if (typeof request.displayName === 'string') {
      actor.displayName = request.displayName;
    }
  }

  async onLeaveActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onDisconnectActor(actor: PlayerActorType): Promise<void> {
    actor.markDisconnected();
  }
}

Inject(ZLINK_SPOT_MANAGER)(BingoEntrySpot, undefined, 0);

export { BingoEntrySpot };

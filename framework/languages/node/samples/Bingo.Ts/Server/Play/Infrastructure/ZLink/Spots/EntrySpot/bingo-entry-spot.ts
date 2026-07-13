import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import {
  BingoRoomJoinReq,
  BingoRoomSettingsPayload,
  ObserveBingoEventsRes
} from '../../../../../../Shared/Contracts/bingo-messages.generated';
import { createObserverRoomSettings } from '../../../../Domain/Bingo/bingo-room-models';
import { BingoRoomSpot } from '../BingoRoomSpot/bingo-room-spot';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type {
  BingoRoomJoinRes,
  EnsurePlayerActorReq,
  ObserveBingoEventsReq
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
      new BingoRoomSettingsPayload({
        ...settings,
        purpose: settings.purpose,
        observedRoomId: settings.observedRoomId
      })
    );
    const joined = await actor.context
      .joinSpot(observerRid, new BingoRoomJoinReq({
        roomId: request.roomId,
        actorId: actor.actorId,
        displayName: actor.displayName,
        observeOnly: true
      }))
      .submit<BingoRoomJoinRes>();
    return new ObserveBingoEventsRes({
      subscribed: joined.status === 'accepted',
      observerNodeRid: String(this.context.nodeRid)
    });
  }

  private observerRoomRid(roomId: string): string {
    return `observe:${roomId}:${String(this.context.nodeRid)}`;
  }

  async onJoinedActor(actor: PlayerActorType): Promise<void> {
    if (!actor.destroyAfterEntrySpotJoin) {
      console.error(`bingo-lifecycle entry-joined actor=${actor.actorId} destroy=false`);
      return;
    }
    console.error(`bingo-lifecycle entry-destroy-start actor=${actor.actorId}`);
    await this.context.destroyActor(actor);
    console.error(`bingo-lifecycle entry-destroy-complete actor=${actor.actorId}`);
  }

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    void actorId;
    void request;
    return { accepted: true };
  }

  async onCreateActor(actor: PlayerActorType, createRequest: ZLinkMessage): Promise<void> {
    const request = createRequest.decode<Partial<EnsurePlayerActorReq>>(Object as never);
    if (typeof request.displayName === 'string') {
      actor.displayName = request.displayName;
    }
  }

  async onLeaveActor(actor: PlayerActorType): Promise<void> {
    console.error(`bingo-lifecycle entry-leave actor=${actor.actorId}`);
  }

  async onDisconnectActor(actor: PlayerActorType): Promise<void> {
    actor.markDisconnected();
  }
}

Inject(ZLINK_SPOT_MANAGER)(BingoEntrySpot, undefined, 0);

export { BingoEntrySpot };

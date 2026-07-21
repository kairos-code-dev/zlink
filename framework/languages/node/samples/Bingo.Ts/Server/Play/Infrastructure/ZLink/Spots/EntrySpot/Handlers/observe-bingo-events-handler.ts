import { Inject } from '@nestjs/common';
import {
  ZLINK_ACTOR_MANAGER,
  ZLINK_SPOT_MANAGER,
  zlinkEntrySpotActorRequestHandler
} from '@zlink-systems/nestjs';
import { BingoEntrySpot } from '../bingo-entry-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkActorManager,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type { PlayerActor as PlayerActorType } from '../../../Actors/player-actor';
import type {
  BingoRoomJoinRes,
  ObserveBingoEventsReq
} from '../../../../../../../Shared/Contracts/messages';
import {
  BingoRoomJoinReq,
  BingoRoomSettingsPayload,
  ObserveBingoEventsRes
} from '../../../../../../../Shared/Contracts/bingo-messages.generated';
import { createObserverRoomSettings } from '../../../../../Domain/Bingo/bingo-room-models';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import { BingoRoomSpot } from '../../BingoRoomSpot/bingo-room-spot';

@zlinkEntrySpotActorRequestHandler({
  actor: () => PlayerActor,
  entrySpot: () => BingoEntrySpot,
  packetName: PacketNames.observeBingoEventsReq
})
class ObserveBingoEventsHandler
  implements ZLinkEntrySpotActorRequestHandler<PlayerActorType, ObserveBingoEventsReq, ObserveBingoEventsRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager
  ) {}

  async handle(
    actor: PlayerActorType,
    context: ZLinkSpotActorRequestContext,
    request: ObserveBingoEventsReq
  ): Promise<ObserveBingoEventsRes> {
    const actorRef = await this.actors.find(SampleNames.roomSpotNode, actor.actorId);
    if (actorRef === undefined) throw new Error(`Bingo actor '${actor.actorId}' is not registered.`);
    const observerNodeRid = String(actorRef.nodeRid);
    const observerRid = `observe:${request.roomId}:${observerNodeRid}`;
    const settings = createObserverRoomSettings(request.roomId, observerNodeRid);
    await this.spots.getOrCreate(
      SampleNames.roomSpotNode,
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
    void context;
    return new ObserveBingoEventsRes({
      subscribed: joined.status === 'accepted',
      observerNodeRid
    });
  }
}

export { ObserveBingoEventsHandler };

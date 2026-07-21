import { Inject } from '@nestjs/common';
import {
  ZLINK_ACTOR_MANAGER,
  ZLINK_CHANNEL_CLIENT,
  zlinkEntrySpotActorRequestHandler
} from '@zlink-systems/nestjs';
import { BingoEntrySpot } from '../bingo-entry-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import {
  BingoRoomJoinReq,
  MatchBingoApiReq,
  MatchBingoRes
} from '../../../../../../../Shared/Contracts/bingo-messages.generated';
import { SampleNames, SampleTimings } from '../../../../../../Configuration/sample-names';
import type {
  ZLinkActorManager,
  ZLinkChannelClient,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { PlayerActor as PlayerActorType } from '../../../Actors/player-actor';
import type {
  BingoRoomJoinRes,
  MatchBingoApiRes,
  MatchBingoReq
} from '../../../../../../../Shared/Contracts/messages';

@zlinkEntrySpotActorRequestHandler({
  actor: () => PlayerActor,
  entrySpot: () => BingoEntrySpot,
  packetName: PacketNames.matchBingoReq
})
class MatchBingoActorHandler
  implements ZLinkEntrySpotActorRequestHandler<PlayerActorType, MatchBingoReq, MatchBingoRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
  ) {}

  async handle(
    actor: PlayerActorType,
    context: ZLinkSpotActorRequestContext,
    request: MatchBingoReq
  ): Promise<MatchBingoRes> {
    console.error(`bingo-match request actor=${actor.actorId}`);
    const actorRef = await this.actors.find(SampleNames.roomSpotNode, actor.actorId);
    if (actorRef === undefined) {
      throw new Error(`Bingo actor '${actor.actorId}' is not registered.`);
    }
    const matched = await this.channels
      .requestToChannel(
        SampleNames.roomSpotNode,
        SampleNames.apiChannel,
        new MatchBingoApiReq({
          actorId: actor.actorId,
          displayName: actor.displayName,
          actorNodeRid: String(actorRef.nodeRid),
          mode: request.mode
        })
      )
      .timeout(SampleTimings.requestTimeout)
      .submit<MatchBingoApiRes>();
    const joined = await actor.context
      .joinSpot(matched.roomId, new BingoRoomJoinReq({
        roomId: matched.roomId,
        actorId: actor.actorId,
        displayName: actor.displayName,
        observeOnly: false
      }))
      .timeout(SampleTimings.requestTimeout)
      .submit<BingoRoomJoinRes>();
    if (joined.status === 'rejected') {
      throw new Error(`Room ${matched.roomId} rejected actor '${actor.actorId}'.`);
    }
    const response = new MatchBingoRes({
      roomId: matched.roomId,
      state: joined.reply.state,
      roomOwnerNodeRid: matched.roomOwnerNodeRid
    });
    console.error(`bingo-match reply actor=${actor.actorId} room=${response.roomId}`);
    void context;
    return response;
  }
}

export { MatchBingoActorHandler };

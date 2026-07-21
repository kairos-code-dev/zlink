import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../../Configuration/sample-settings';
import {
  PacketNames,
  authenticatePlayerReq,
  authenticateRes
} from '../../../../../../Shared/Contracts/messages';
import {
  ZLinkPacket,
  type ZLinkActorManager,
  type ZLinkChannelClient,
  type ZLinkMessage,
  type ZLinkSessionContext,
  type ZLinkSessionDispatchContext
} from '@zlink-systems/framework';
import type { AuthenticatePlayerRes, AuthenticateReq } from '../../../../../../Shared/Contracts/messages';

@Injectable()
@ZLinkPacket(PacketNames.authenticateReq)
class AuthenticatePlaySessionHandler {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly api: ZLinkChannelClient
  ) {}

  async handle(context: ZLinkSessionContext, _dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    const request = payload.decode<AuthenticateReq>(Object as never);
    const authenticated = await this.api
      .requestToChannel(
        SampleNames.playSpotNode,
        SampleNames.apiChannel,
        authenticatePlayerReq(request.accessToken)
      )
      .submit<AuthenticatePlayerRes>();
    const actorRef = await this.actors.getOrCreate(
      SampleNames.playSpotNode,
      authenticated.player.actorId,
      SampleNames.playerActorType,
      authenticated.player
    );
    await context.actors.bindOrGet(actorRef);
    context.client.reply(authenticateRes(authenticated.player)).submit();
  }
}

export { AuthenticatePlaySessionHandler };

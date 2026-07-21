import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Configuration/sample-names';
import { PacketNames, allocateConversation } from '../../../Shared/Contracts/messages';
import type { ZLinkChannelClient, ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  AllocateConversationRes,
  OpenConversationApiReq,
  OpenConversationApiRes
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.openConversationApiReq)
class OpenConversationHandler implements ZLinkRequestHandler<OpenConversationApiReq, OpenConversationApiRes> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  async handle(request: OpenConversationApiReq): Promise<OpenConversationApiRes> {
    return await this.channels
      .requestToChannel(
        SampleNames.conversationSpotMesh,
        SampleNames.supportChannel,
        allocateConversation(request.customerActorId, request.customerDisplayName, request.subject)
      )
      .timeout(2000)
      .submit<AllocateConversationRes>();
  }
}

export { OpenConversationHandler };

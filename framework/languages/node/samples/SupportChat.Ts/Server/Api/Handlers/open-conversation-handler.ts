import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import {
  PacketNames,
  allocateConversationReq,
  assignAgentReq,
  openConversationApiRes
} from '../../../Shared/Contracts/messages';
import { SampleNames } from '../../Configuration/sample-names';
import type { ZLinkChannelClient, ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  AllocateConversationRes,
  AssignAgentRes,
  OpenConversationApiReq,
  OpenConversationApiRes
} from '../../../Shared/Contracts/messages';

// OpenConversationHandler orchestrates the conversation start: it allocates a conversation
// on the Support server and asks the Support server to assign an available agent.
@zlinkRequestHandler('api', PacketNames.openConversationApiReq)
class OpenConversationHandler implements ZLinkRequestHandler<OpenConversationApiReq, OpenConversationApiRes> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  async handle(request: OpenConversationApiReq): Promise<OpenConversationApiRes> {
    const allocated = await this.channels
      .requestToChannel(
        SampleNames.supportChannel,
        allocateConversationReq(request.customerActorId, request.customerDisplayName, request.subject)
      )
      .packetName(PacketNames.allocateConversationReq)
      .submit<AllocateConversationRes>();
    const assigned = await this.channels
      .requestToChannel(SampleNames.supportChannel, assignAgentReq(allocated.conversationId, null))
      .packetName(PacketNames.assignAgentReq)
      .submit<AssignAgentRes>();
    return openConversationApiRes(allocated.conversationId, assigned.status);
  }
}

export { OpenConversationHandler };

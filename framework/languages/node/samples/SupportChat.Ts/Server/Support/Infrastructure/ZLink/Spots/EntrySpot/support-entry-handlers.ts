import { Inject } from '@nestjs/common';
import {
  ZLINK_CHANNEL_CLIENT,
  zlinkEntrySpotActorRequestHandler
} from '@zlink-systems/nestjs';
import { AgentAvailabilityDirectory } from '../../../../Application/ConversationAssignment/agent-availability-directory';
import { SampleNames, SampleTimings } from '../../../../../Configuration/sample-names';
import {
  PacketNames,
  SupportChatRoles,
  joinConversation,
  openConversationApi
} from '../../../../../../Shared/Contracts/messages';
import { SupportUserActor } from '../../Actors/support-user-actor';
import { SupportEntrySpot } from './support-entry-spot';
import type {
  OpenConversationApiRes,
  OpenConversationReq,
  OpenConversationRes,
  SetAgentAvailableReq,
  SetAgentAvailableRes
} from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkChannelClient,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';

@zlinkEntrySpotActorRequestHandler({
  actor: () => SupportUserActor,
  entrySpot: () => SupportEntrySpot,
  packetName: PacketNames.setAgentAvailableReq
})
class SetAgentAvailableHandler implements ZLinkEntrySpotActorRequestHandler<SupportEntrySpot, SupportUserActor, SetAgentAvailableReq, SetAgentAvailableRes> {
  constructor(private readonly availability: AgentAvailabilityDirectory) {}

  async handle(_spot: SupportEntrySpot, actor: SupportUserActor, _context: ZLinkSpotActorRequestContext, request: SetAgentAvailableReq): Promise<SetAgentAvailableRes> {
    if (actor.role !== SupportChatRoles.Agent || actor.actorId !== actor.participantId) {
      throw new Error('Customer actor must not set agent availability.');
    }
    return { isAvailable: this.availability.setAvailable(actor.actorId, request.isAvailable) };
  }
}

@zlinkEntrySpotActorRequestHandler({
  actor: () => SupportUserActor,
  entrySpot: () => SupportEntrySpot,
  packetName: PacketNames.openConversationReq
})
class OpenConversationActorHandler implements ZLinkEntrySpotActorRequestHandler<SupportEntrySpot, SupportUserActor, OpenConversationReq, OpenConversationRes> {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  async handle(_spot: SupportEntrySpot, actor: SupportUserActor, _context: ZLinkSpotActorRequestContext, request: OpenConversationReq): Promise<OpenConversationRes> {
    if (actor.role !== SupportChatRoles.Customer) {
      throw new Error('Only a customer can open a support conversation.');
    }
    const opened = await this.channels
      .requestToChannel(SampleNames.apiChannel, openConversationApi(actor.actorId, actor.displayName, request.subject))
      .timeout(SampleTimings.requestTimeout)
      .submit<OpenConversationApiRes>();
    const joined = await actor.context.joinSpot(
      opened.conversationId,
      joinConversation(actor.participantId, actor.role, actor.displayName)
    ).submit<{ state: OpenConversationRes['state'] }>();
    if (joined.status !== 'accepted') {
      throw new Error(`Conversation '${opened.conversationId}' rejected customer actor.`);
    }
    return {
      conversationId: opened.conversationId,
      state: joined.reply.state
    };
  }
}

export { SetAgentAvailableHandler, OpenConversationActorHandler };

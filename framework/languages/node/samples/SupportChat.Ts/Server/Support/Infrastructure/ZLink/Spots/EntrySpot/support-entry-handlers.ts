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
import { SupportActorDirectory } from '../../Actors/support-actor-directory';
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
class SetAgentAvailableHandler implements ZLinkEntrySpotActorRequestHandler<SupportUserActor, SetAgentAvailableReq, SetAgentAvailableRes> {
  constructor(
    private readonly availability: AgentAvailabilityDirectory,
    private readonly directory: SupportActorDirectory
  ) {}

  async handle(actor: SupportUserActor, _context: ZLinkSpotActorRequestContext, request: SetAgentAvailableReq): Promise<SetAgentAvailableRes> {
    const identity = requireIdentity(this.directory, actor.actorId);
    if (identity.role !== SupportChatRoles.Agent || identity.actorId !== identity.participantId) {
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
class OpenConversationActorHandler implements ZLinkEntrySpotActorRequestHandler<SupportUserActor, OpenConversationReq, OpenConversationRes> {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient,
    private readonly directory: SupportActorDirectory
  ) {}

  async handle(actor: SupportUserActor, _context: ZLinkSpotActorRequestContext, request: OpenConversationReq): Promise<OpenConversationRes> {
    const identity = requireIdentity(this.directory, actor.actorId);
    if (identity.role !== SupportChatRoles.Customer) {
      throw new Error('Only a customer can open a support conversation.');
    }
    const opened = await this.channels
      .requestToChannel(
        SampleNames.conversationSpotMesh,
        SampleNames.apiChannel,
        openConversationApi(identity.actorId, identity.displayName, request.subject)
      )
      .timeout(SampleTimings.requestTimeout)
      .submit<OpenConversationApiRes>();
    const joined = await actor.context.joinSpot(
      opened.conversationId,
      joinConversation(identity.participantId, identity.role, identity.displayName)
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

function requireIdentity(directory: SupportActorDirectory, actorId: string) {
  const identity = directory.get(actorId);
  if (identity === undefined) throw new Error(`Support actor '${actorId}' identity was not found.`);
  return identity;
}

export { SetAgentAvailableHandler, OpenConversationActorHandler };

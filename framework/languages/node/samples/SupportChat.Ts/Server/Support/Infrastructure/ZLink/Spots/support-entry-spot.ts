import { Inject } from '@nestjs/common';
import { AgentAvailabilityDirectory } from '../../../Application/ConversationAssignment/agent-availability-directory';
import { SampleNames } from '../../../../Configuration/sample-names';
import {
  PacketNames,
  SupportChatRoles,
  openConversationApiReq,
  openConversationRes
} from '../../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage
} from '@zlink-systems/framework';
import type { AgentAvailabilityDirectory as AgentAvailabilityDirectoryType } from '../../../Application/ConversationAssignment/agent-availability-directory';
import type { SupportUserActor as SupportUserActorType } from '../Actors/support-user-actor';
import type {
  EnsureSupportUserActorReq,
  JoinConversationRes,
  OpenConversationApiRes,
  OpenConversationReq,
  OpenConversationRes,
  SetAgentAvailableReq,
  SetAgentAvailableRes
} from '../../../../../Shared/Contracts/messages';

// SupportEntrySpot is the actor admission point before an actor enters a conversation.
// Customers ask the API channel to allocate and assign the conversation, then the
// customer actor joins the created conversation Spot.
class SupportEntrySpot implements ZLinkEntrySpot<SupportUserActorType> {
  readonly context!: ZLinkEntrySpotContext<SupportUserActorType>;

  constructor(
    private readonly availability: AgentAvailabilityDirectoryType
  ) {}

  async openConversation(actor: SupportUserActorType, request: OpenConversationReq & { actorId: string; displayName: string; role?: string }): Promise<OpenConversationRes> {
    if ((request.role ?? SupportChatRoles.customer) !== SupportChatRoles.customer) {
      throw new Error('Only customer actors can open a conversation.');
    }

    const opened = await this.context.outbound
      .requestToChannel(
        SampleNames.apiChannel,
        openConversationApiReq(request.actorId, request.displayName, request.subject)
      )
      .packetName(PacketNames.openConversationApiReq)
      .submit<OpenConversationApiRes>();

    const joined = await actor.context
      .joinSpot(opened.conversationId)
      .submit<Partial<JoinConversationRes & { error: string }>>();
    if (joined.resultCode !== 0) {
      throw new Error(joined.reply?.error ?? `Conversation '${opened.conversationId}' rejected actor '${actor.actorId}'.`);
    }
    const state = (joined.reply as JoinConversationRes).state;
    return openConversationRes(opened.conversationId, state);
  }

  setAgentAvailable(request: SetAgentAvailableReq & { actorId: string; displayName: string; role?: string }): SetAgentAvailableRes {
    if ((request.role ?? SupportChatRoles.agent) !== SupportChatRoles.agent) {
      throw new Error('Only agent actors can set availability.');
    }
    this.availability.setAvailable(request.actorId, request.displayName, request.isAvailable);
    return { isAvailable: request.isAvailable };
  }

  async onCreateActor(actor: SupportUserActorType, createRequest: ZLinkMessage): Promise<void> {
    const request = createRequest.decode<Partial<EnsureSupportUserActorReq>>(Object as never);
    if (typeof request.displayName === 'string' && typeof request.role === 'string') {
      actor.setIdentity(request.displayName, request.role);
    }
  }

  async onJoinedActor(actor: SupportUserActorType): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: SupportUserActorType): Promise<void> {
    void actor;
  }

  async onDisconnectActor(actor: SupportUserActorType): Promise<void> {
    actor.markDisconnected();
    if (actor.role === SupportChatRoles.agent) {
      this.availability.setAvailable(actor.actorId, actor.displayName, false);
    }
  }
}

Inject(AgentAvailabilityDirectory)(SupportEntrySpot, undefined, 0);

export { SupportEntrySpot };

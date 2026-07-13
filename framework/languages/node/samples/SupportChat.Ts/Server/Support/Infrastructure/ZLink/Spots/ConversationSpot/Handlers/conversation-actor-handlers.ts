import { zlinkSpotActorRequestHandler, zlinkSpotActorSendHandler, zlinkSpotPacketHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import { AgentAvailabilityDirectory } from '../../../../../Application/ConversationAssignment/agent-availability-directory';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import { SupportUserActor } from '../../../Actors/support-user-actor';
import { ConversationSpot } from '../conversation-spot';
import { AgentAssignmentService } from '../../../../../Application/ConversationAssignment/agent-assignment-service';
import { SupportActorDirectory } from '../../../Actors/support-actor-directory';
import { SupportNotificationPublisher } from '../Notifications/support-notification-publisher';
import { EnsureConversationAssignmentReq } from '../ConversationContracts';
import type {
  CloseConversationReq,
  CloseConversationRes,
  JoinConversationReq,
  JoinConversationRes,
  SendChatMessageReq,
  SendChatMessageRes,
  SetTypingReq
} from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';

@zlinkSpotPacketHandler({ packetName: 'EnsureConversationAssignmentReq', spot: () => ConversationSpot })
class EnsureConversationAssignmentHandler
  implements ZLinkSpotRequestHandler<ConversationSpot, EnsureConversationAssignmentReq, { state: import('../../../../../../../Shared/Contracts/messages').ConversationState }> {
  constructor(
    private readonly assignments: AgentAssignmentService,
    private readonly actors: SupportActorDirectory,
    private readonly notifications: SupportNotificationPublisher
  ) {}

  async handle(spot: ConversationSpot): Promise<{ state: import('../../../../../../../Shared/Contracts/messages').ConversationState }> {
    const current = spot.snapshot();
    if (current.agentActorId !== undefined) return { state: current };
    const agentActorId = this.assignments.assignNextAgent();
    if (agentActorId === undefined) return { state: current };
    const roster = this.actors.get(agentActorId);
    if (roster === undefined) {
      throw new Error(`Assigned roster actor '${agentActorId}' was not found.`);
    }
    const assigned = spot.assignAgent(agentActorId, roster.displayName);
    this.notifications.publish(assigned.event, [roster]);
    return { state: assigned.state };
  }
}

function hasConversation(context: { metadata: { find(key: string): string | undefined } }, spot: ConversationSpot): boolean {
  return context.metadata.find(SampleNames.conversationIdMetadataKey) === spot.snapshot().conversationId;
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.joinConversationReq })
class JoinConversationHandler implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, JoinConversationReq, JoinConversationRes> {
  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorRequestContext): Promise<JoinConversationRes> {
    if (!hasConversation(context, spot)) throw new Error('Conversation metadata does not match the bound actor.');
    return { state: spot.join(actor) };
  }
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.sendChatMessageReq })
class SendChatMessageHandler implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, SendChatMessageReq, SendChatMessageRes> {
  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorRequestContext, request: SendChatMessageReq): Promise<SendChatMessageRes> {
    if (!hasConversation(context, spot)) throw new Error('Conversation metadata does not match the bound actor.');
    return spot.sendChat(actor, request.text);
  }
}

@zlinkSpotActorSendHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.setTypingReq })
class SetTypingHandler implements ZLinkSpotActorSendHandler<ConversationSpot, SupportUserActor, SetTypingReq> {
  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorSendContext, request: SetTypingReq): Promise<void> {
    if (hasConversation(context, spot)) spot.setTyping(actor, request.isTyping);
  }
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.closeConversationReq })
class CloseConversationHandler implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, CloseConversationReq, CloseConversationRes> {
  constructor(private readonly availability: AgentAvailabilityDirectory) {}

  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorRequestContext): Promise<CloseConversationRes> {
    if (!hasConversation(context, spot)) throw new Error('Conversation metadata does not match the bound actor.');
    const state = spot.close(actor);
    if (state.agentActorId !== undefined) this.availability.released(state.agentActorId);
    return { state };
  }
}

export {
  EnsureConversationAssignmentHandler,
  JoinConversationHandler,
  SendChatMessageHandler,
  SetTypingHandler,
  CloseConversationHandler
};

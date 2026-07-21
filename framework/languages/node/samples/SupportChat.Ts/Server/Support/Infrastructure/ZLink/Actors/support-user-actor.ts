import { ZLinkSpotActorRequest, ZLinkSpotActorSend } from '@zlink-systems/framework';
import { joinConversation } from '../../../../../Shared/Contracts/messages';
import {
  ChatMessageNotify,
  ConversationAssignedNotify,
  ConversationClosedNotify,
  ConversationIdleNotify,
  ParticipantJoinedNotify,
  TypingChangedNotify
} from '../../../../../Shared/Contracts/messages';
import type { ConversationState } from '../../../../../Shared/Contracts/messages';
import type { SupportRole } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActor, ZLinkActorContext } from '@zlink-systems/framework';

class DeliverSupportNotification {
  readonly packetName: string;

  constructor(readonly message: unknown, readonly conversationId: string) {
    this.packetName = typeof message === 'object' && message !== null
      ? message.constructor.name
      : '';
  }
}

class JoinSupportConversation {
  constructor(
    readonly conversationId: string,
    readonly participantId: string,
    readonly role: SupportRole,
    readonly displayName: string
  ) {}
}

class SupportUserActor implements ZLinkActor {
  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}

  configure(): void {
    this.context.handlers.addHandler(DeliverSupportNotificationHandler);
    this.context.handlers.addHandler(JoinSupportConversationHandler);
  }

  push(message: unknown, conversationId: string): void {
    try {
      this.context.boundSession
        .send(message)
        .metadata('conversation-id', conversationId)
        .submit();
    } catch {
      // Conversation state remains in the Spot and is returned after reconnect.
    }
  }
}

class DeliverSupportNotificationHandler {
  @ZLinkSpotActorSend('DeliverSupportNotification')
  async handle(actor: SupportUserActor, _context: unknown, message: DeliverSupportNotification): Promise<void> {
    actor.push(rehydrateSupportNotification(message.packetName, message.message), message.conversationId);
  }
}

function rehydrateSupportNotification(packetName: string, payload: unknown): unknown {
  const value = payload as Record<string, unknown>;
  switch (packetName) {
    case 'ParticipantJoinedNotify':
      return new ParticipantJoinedNotify(
        value.conversationId as string,
        value.actorId as string,
        value.role as SupportRole,
        value.state as ConversationState
      );
    case 'ConversationAssignedNotify':
      return new ConversationAssignedNotify(value.conversationId as string, value.state as ConversationState);
    case 'ChatMessageNotify':
      return new ChatMessageNotify(
        value.conversationId as string,
        value.message as never,
        value.state as ConversationState
      );
    case 'TypingChangedNotify':
      return new TypingChangedNotify(
        value.conversationId as string,
        value.actorId as string,
        value.isTyping as boolean,
        value.state as ConversationState
      );
    case 'ConversationIdleNotify':
      return new ConversationIdleNotify(value.conversationId as string, value.state as ConversationState);
    case 'ConversationClosedNotify':
      return new ConversationClosedNotify(value.conversationId as string, value.state as ConversationState);
    default:
      throw new Error(`Unsupported SupportChat notification '${packetName}'.`);
  }
}

class JoinSupportConversationHandler {
  @ZLinkSpotActorRequest('JoinSupportConversation')
  async handle(
    actor: SupportUserActor,
    _context: unknown,
    message: JoinSupportConversation
  ): Promise<{ readonly state: ConversationState }> {
    const joined = await actor.context.joinSpot(
      message.conversationId,
      joinConversation(message.participantId, message.role, message.displayName)
    ).submit<{ state: ConversationState }>();
    if (joined.status !== 'accepted') {
      throw new Error(`Conversation '${message.conversationId}' rejected actor '${actor.actorId}'.`);
    }
    return joined.reply;
  }
}

export {
  DeliverSupportNotification,
  DeliverSupportNotificationHandler,
  JoinSupportConversation,
  JoinSupportConversationHandler,
  SupportUserActor
};

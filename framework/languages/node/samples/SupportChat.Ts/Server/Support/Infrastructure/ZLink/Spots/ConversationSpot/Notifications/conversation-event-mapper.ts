import { ConversationEventKind } from '../../../../../Domain/SupportChat/conversation-models';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import type { ConversationEvent } from '../../../../../Domain/SupportChat/conversation-models';
import type {
  ChatMessageNotify,
  ConversationAssignedNotify,
  ConversationClosedNotify,
  ConversationIdleNotify,
  ParticipantJoinedNotify,
  TypingChangedNotify
} from '../../../../../../../Shared/Contracts/messages';

type MappedNotification = {
  actorId: string;
  packetName: string;
  payload: unknown;
};

// ConversationEventMapper turns a domain ConversationEvent into the set of bound-session
// push messages and their target actor ids, following the common SupportChat contract.
// It contains no domain judgement: the conversation already decided what happened.
class ConversationEventMapper {
  map(conversationEvent: ConversationEvent, participantActorIds: string[]): MappedNotification[] {
    switch (conversationEvent.kind) {
      case ConversationEventKind.ParticipantJoined:
        return this.participantJoined(conversationEvent, participantActorIds);
      case ConversationEventKind.Assigned:
        return this.assigned(conversationEvent);
      case ConversationEventKind.MessageAppended:
        return this.messageAppended(conversationEvent, participantActorIds);
      case ConversationEventKind.TypingChanged:
        return this.typingChanged(conversationEvent, participantActorIds);
      case ConversationEventKind.Idle:
        return this.idle(conversationEvent, participantActorIds);
      case ConversationEventKind.Closed:
        return this.closed(conversationEvent, participantActorIds);
      default:
        throw new Error(`Unsupported conversation event ${String(conversationEvent.kind)}.`);
    }
  }

  private participantJoined(conversationEvent: ConversationEvent, participantActorIds: string[]): MappedNotification[] {
    if (conversationEvent.actorId === undefined || conversationEvent.role === undefined) {
      throw new Error('Participant joined event requires actor id and role.');
    }
    const customerActorId = conversationEvent.state.customerActorId;
    if (!participantActorIds.includes(customerActorId) || customerActorId === conversationEvent.actorId) {
      return [];
    }
    const payload: ParticipantJoinedNotify = {
      conversationId: conversationEvent.state.conversationId,
      actorId: conversationEvent.actorId,
      role: conversationEvent.role,
      state: conversationEvent.state
    };
    return [{ actorId: customerActorId, packetName: SampleNames.participantJoinedPacket, payload }];
  }

  private assigned(conversationEvent: ConversationEvent): MappedNotification[] {
    if (conversationEvent.actorId === undefined) {
      return [];
    }
    const payload: ConversationAssignedNotify = {
      conversationId: conversationEvent.state.conversationId,
      state: conversationEvent.state
    };
    return [{ actorId: conversationEvent.actorId, packetName: SampleNames.conversationAssignedPacket, payload }];
  }

  private messageAppended(conversationEvent: ConversationEvent, participantActorIds: string[]): MappedNotification[] {
    const message = conversationEvent.message;
    if (message === undefined) {
      throw new Error('Message event requires a chat message.');
    }
    const payload: ChatMessageNotify = {
      conversationId: conversationEvent.state.conversationId,
      message,
      state: conversationEvent.state
    };
    return participantActorIds
      .filter((actorId) => actorId !== message.senderActorId)
      .map((actorId) => ({ actorId, packetName: SampleNames.chatMessagePacket, payload }));
  }

  private typingChanged(conversationEvent: ConversationEvent, participantActorIds: string[]): MappedNotification[] {
    if (conversationEvent.actorId === undefined || conversationEvent.isTyping === undefined) {
      throw new Error('Typing event requires actor id and typing state.');
    }
    const payload: TypingChangedNotify = {
      conversationId: conversationEvent.state.conversationId,
      actorId: conversationEvent.actorId,
      isTyping: conversationEvent.isTyping,
      state: conversationEvent.state
    };
    return participantActorIds
      .filter((actorId) => actorId !== conversationEvent.actorId)
      .map((actorId) => ({ actorId, packetName: SampleNames.typingChangedPacket, payload }));
  }

  private idle(conversationEvent: ConversationEvent, participantActorIds: string[]): MappedNotification[] {
    const payload: ConversationIdleNotify = {
      conversationId: conversationEvent.state.conversationId,
      state: conversationEvent.state
    };
    return participantActorIds.map((actorId) => ({ actorId, packetName: SampleNames.conversationIdlePacket, payload }));
  }

  private closed(conversationEvent: ConversationEvent, participantActorIds: string[]): MappedNotification[] {
    const payload: ConversationClosedNotify = {
      conversationId: conversationEvent.state.conversationId,
      state: conversationEvent.state
    };
    return participantActorIds.map((actorId) => ({ actorId, packetName: SampleNames.conversationClosedPacket, payload }));
  }
}

export { ConversationEventMapper };
export type { MappedNotification };

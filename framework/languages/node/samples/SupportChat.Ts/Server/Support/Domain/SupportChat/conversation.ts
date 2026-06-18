import { ConversationEventKind, sampleConversationPolicy } from './conversation-models';
import { ConversationStatuses, SupportChatRoles } from '../../../../Shared/Contracts/messages';
import type {
  ConversationChange,
  ConversationMessage,
  ConversationParticipant,
  ConversationPolicy
} from './conversation-models';
import type { ChatMessage, ConversationState } from '../../../../Shared/Contracts/messages';

// Conversation is the aggregate root. Message sequence, participant membership, typing,
// idle and close transitions all stay inside the domain. The domain never touches ZLink
// actors, Spots, sessions, channel clients or timers; it returns ConversationEvent values
// that the adapter layer turns into push messages.
class Conversation {
  private readonly policy: ConversationPolicy;
  private readonly participants = new Map<string, ConversationParticipant>();
  private readonly messages: ConversationMessage[] = [];
  private lastMessageSeq = 0;
  private lastMessageAtUnixMs: number | null = null;
  private idleDeadlineUnixMs: number | null = null;
  private status: string;
  private agentActorId: string | null = null;

  constructor(
    readonly conversationId: string,
    readonly subject: string,
    readonly customerActorId: string,
    customerDisplayName: string,
    createdAtUnixMs: number,
    policy: ConversationPolicy = sampleConversationPolicy()
  ) {
    if (subject.trim().length === 0) {
      throw new Error('Conversation subject is required.');
    }
    this.policy = policy;
    this.status = ConversationStatuses.WaitingForAgent;
    this.participants.set(customerActorId, {
      actorId: customerActorId,
      role: SupportChatRoles.customer,
      displayName: customerDisplayName,
      joinedAtUnixMs: createdAtUnixMs,
      isTyping: false
    });
  }

  snapshot(): ConversationState {
    return {
      conversationId: this.conversationId,
      subject: this.subject,
      status: this.status,
      customerActorId: this.customerActorId,
      agentActorId: this.agentActorId,
      lastMessageSeq: this.lastMessageSeq,
      lastMessageAtUnixMs: this.lastMessageAtUnixMs,
      idleDeadlineUnixMs: this.idleDeadlineUnixMs
    };
  }

  joinAgent(agentActorId: string, agentDisplayName: string, joinedAtUnixMs: number): ConversationChange {
    this.ensureNotClosed('join an agent');
    if (this.agentActorId !== null && this.agentActorId !== agentActorId) {
      throw new Error('Conversation already has an assigned agent.');
    }

    this.agentActorId = agentActorId;
    this.status = ConversationStatuses.Active;
    this.participants.set(agentActorId, {
      actorId: agentActorId,
      role: SupportChatRoles.agent,
      displayName: agentDisplayName,
      joinedAtUnixMs,
      isTyping: false
    });

    const state = this.snapshot();
    return {
      state,
      events: [
        { kind: ConversationEventKind.ParticipantJoined, state, actorId: agentActorId, role: SupportChatRoles.agent },
        { kind: ConversationEventKind.Assigned, state, actorId: agentActorId, role: SupportChatRoles.agent }
      ]
    };
  }

  sendMessage(senderActorId: string, text: string, sentAtUnixMs: number): ConversationChange {
    this.ensureParticipant(senderActorId);
    if (this.status === ConversationStatuses.Closed) {
      throw new Error('Closed conversation cannot accept messages.');
    }
    if (this.status === ConversationStatuses.WaitingForAgent) {
      throw new Error('Conversation is waiting for an agent.');
    }
    if (text.trim().length === 0) {
      throw new Error('Message text is required.');
    }
    if (text.length > this.policy.maxMessageLength) {
      throw new Error('Message text is too long.');
    }

    this.status = ConversationStatuses.Active;
    this.lastMessageSeq += 1;
    this.lastMessageAtUnixMs = sentAtUnixMs;
    this.idleDeadlineUnixMs = sentAtUnixMs + this.policy.idleTimeoutMs;
    const message: ConversationMessage = {
      conversationId: this.conversationId,
      messageSeq: this.lastMessageSeq,
      senderActorId,
      text,
      sentAtUnixMs
    };
    this.messages.push(message);

    const state = this.snapshot();
    return {
      state,
      events: [
        { kind: ConversationEventKind.MessageAppended, state, actorId: senderActorId, message: toContract(message) }
      ]
    };
  }

  setTyping(actorId: string, isTyping: boolean): ConversationChange {
    const participant = this.ensureParticipant(actorId);
    this.ensureNotClosed('change typing state');

    this.participants.set(actorId, { ...participant, isTyping });
    const state = this.snapshot();
    return {
      state,
      events: [
        { kind: ConversationEventKind.TypingChanged, state, actorId, role: participant.role, isTyping }
      ]
    };
  }

  markIdle(nowUnixMs: number): ConversationChange {
    if (
      this.status !== ConversationStatuses.Active ||
      this.idleDeadlineUnixMs === null ||
      nowUnixMs < this.idleDeadlineUnixMs
    ) {
      return { state: this.snapshot(), events: [] };
    }

    this.status = ConversationStatuses.WaitingForClose;
    const state = this.snapshot();
    return { state, events: [{ kind: ConversationEventKind.Idle, state }] };
  }

  close(actorId: string, reason: string | null): ConversationChange {
    void reason;
    this.ensureParticipant(actorId);
    if (this.status === ConversationStatuses.Closed) {
      throw new Error('Conversation is already closed.');
    }

    this.status = ConversationStatuses.Closed;
    const state = this.snapshot();
    return { state, events: [{ kind: ConversationEventKind.Closed, state, actorId }] };
  }

  private ensureParticipant(actorId: string): ConversationParticipant {
    const participant = this.participants.get(actorId);
    if (participant === undefined) {
      throw new Error('Actor is not a conversation participant.');
    }
    return participant;
  }

  private ensureNotClosed(action: string): void {
    if (this.status === ConversationStatuses.Closed) {
      throw new Error(`Closed conversation cannot ${action}.`);
    }
  }
}

function toContract(message: ConversationMessage): ChatMessage {
  return {
    conversationId: message.conversationId,
    messageSeq: message.messageSeq,
    senderActorId: message.senderActorId,
    text: message.text,
    sentAtUnixMs: message.sentAtUnixMs
  };
}

export { Conversation };

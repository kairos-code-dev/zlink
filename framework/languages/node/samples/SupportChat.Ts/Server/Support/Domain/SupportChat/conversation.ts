import { ConversationStatuses } from './conversation-models';
import type { ChatMessage, ConversationState } from './conversation-models';

class Conversation {
  private readonly messages: ChatMessage[] = [];
  private state: ConversationState;

  constructor(conversationId: string, customerActorId: string, subject: string) {
    this.state = {
      conversationId,
      subject,
      status: ConversationStatuses.WaitingForAgent,
      customerActorId,
      lastMessageSeq: 0
    };
  }

  snapshot(): ConversationState {
    return { ...this.state };
  }

  assign(agentActorId: string): ConversationState {
    if (this.state.status === ConversationStatuses.Closed) {
      throw new Error('Closed conversation cannot be assigned.');
    }
    this.state = { ...this.state, agentActorId };
    return this.snapshot();
  }

  join(actorId: string): ConversationState {
    if (this.state.status === ConversationStatuses.Closed) {
      return this.snapshot();
    }
    if (actorId === this.state.agentActorId) {
      this.state = { ...this.state, status: ConversationStatuses.Active };
    }
    return this.snapshot();
  }

  appendMessage(senderActorId: string, text: string): { message: ChatMessage; state: ConversationState } {
    if (this.state.status === ConversationStatuses.Closed || this.state.status === ConversationStatuses.WaitingForClose) {
      throw new Error('Closed conversation must reject messages.');
    }
    const now = Date.now();
    const message: ChatMessage = {
      conversationId: this.state.conversationId,
      messageSeq: this.state.lastMessageSeq + 1,
      senderActorId,
      text,
      sentAtUnixMs: now
    };
    this.messages.push(message);
    this.state = {
      ...this.state,
      lastMessageSeq: message.messageSeq,
      lastMessageAtUnixMs: now
    };
    return { message, state: this.snapshot() };
  }

  markIdle(deadlineUnixMs: number): ConversationState {
    if (this.state.status === ConversationStatuses.Closed) {
      return this.snapshot();
    }
    this.state = {
      ...this.state,
      status: ConversationStatuses.WaitingForClose,
      idleDeadlineUnixMs: deadlineUnixMs
    };
    return this.snapshot();
  }

  close(): ConversationState {
    if (this.state.status === ConversationStatuses.Closed) {
      throw new Error('Closed conversation must reject a duplicate close.');
    }
    this.state = {
      ...this.state,
      status: ConversationStatuses.Closed,
      idleDeadlineUnixMs: undefined
    };
    return this.snapshot();
  }
}

export { Conversation };

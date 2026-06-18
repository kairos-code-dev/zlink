import { SampleTimings } from '../../../Configuration/sample-names';
import type { ChatMessage, ConversationState } from '../../../../Shared/Contracts/messages';

type ConversationPolicy = {
  idleTimeoutMs: number;
  closeGraceTimeoutMs: number;
  maxMessageLength: number;
};

function sampleConversationPolicy(): ConversationPolicy {
  return {
    idleTimeoutMs: SampleTimings.idleTimeoutMs,
    closeGraceTimeoutMs: SampleTimings.closeGraceTimeoutMs,
    maxMessageLength: SampleTimings.maxMessageLength
  };
}

type ConversationParticipant = {
  actorId: string;
  role: string;
  displayName: string;
  joinedAtUnixMs: number;
  isTyping: boolean;
};

type ConversationMessage = {
  conversationId: string;
  messageSeq: number;
  senderActorId: string;
  text: string;
  sentAtUnixMs: number;
};

enum ConversationEventKind {
  ParticipantJoined = 'ParticipantJoined',
  Assigned = 'Assigned',
  MessageAppended = 'MessageAppended',
  TypingChanged = 'TypingChanged',
  Idle = 'Idle',
  Closed = 'Closed'
}

type ConversationEvent = {
  kind: ConversationEventKind;
  state: ConversationState;
  actorId?: string;
  role?: string;
  message?: ChatMessage;
  isTyping?: boolean;
};

type ConversationChange = {
  state: ConversationState;
  events: ConversationEvent[];
};

export { ConversationEventKind, sampleConversationPolicy };
export type {
  ConversationChange,
  ConversationEvent,
  ConversationMessage,
  ConversationParticipant,
  ConversationPolicy
};

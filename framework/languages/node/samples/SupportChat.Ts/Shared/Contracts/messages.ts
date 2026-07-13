type SupportRole = 'Agent' | 'Customer';
type ConversationStatus = 'WaitingForAgent' | 'Active' | 'WaitingForClose' | 'Closed';

class AuthenticateReq { constructor(readonly accessToken: string) {} }
type AuthenticateRes = { actorId: string; displayName: string; role: SupportRole };
class AuthenticateUserReq { constructor(readonly accessToken: string) {} }
type AuthenticateUserRes = {
  accepted: boolean;
  actorId?: string;
  displayName?: string;
  role?: SupportRole;
  reason?: string;
};
class OpenConversationApiReq {
  constructor(readonly customerActorId: string, readonly customerDisplayName: string, readonly subject: string) {}
}
type OpenConversationApiRes = { conversationId: string; status: ConversationStatus };
class AllocateConversationReq {
  constructor(readonly customerActorId: string, readonly customerDisplayName: string, readonly subject: string) {}
}
type AllocateConversationRes = { conversationId: string; status: ConversationStatus };
class AssignAgentReq { constructor(readonly conversationId: string, readonly agentActorId: string) {} }
type AssignAgentRes = { conversationId: string; assigned: boolean };
class OpenConversationReq { constructor(readonly subject: string) {} }
type OpenConversationRes = { conversationId: string; state: ConversationState };
class SetAgentAvailableReq { constructor(readonly isAvailable: boolean) {} }
type SetAgentAvailableRes = { isAvailable: boolean };
class JoinConversationReq { constructor(readonly conversationId: string) {} }
type JoinConversationRes = { state: ConversationState };
class SendChatMessageReq { constructor(readonly conversationId: string, readonly text: string) {} }
type SendChatMessageRes = { message: ChatMessage; state: ConversationState };
class SetTypingReq { constructor(readonly conversationId: string, readonly isTyping: boolean) {} }
class CloseConversationReq { constructor(readonly conversationId: string, readonly reason?: string) {} }
type CloseConversationRes = { state: ConversationState };
type ParticipantJoinedNotify = { conversationId: string; actorId: string; role: SupportRole; state: ConversationState };
type ConversationAssignedNotify = { conversationId: string; state: ConversationState };
type ChatMessageNotify = { conversationId: string; message: ChatMessage; state: ConversationState };
type TypingChangedNotify = { conversationId: string; actorId: string; isTyping: boolean; state: ConversationState };
type ConversationIdleNotify = { conversationId: string; state: ConversationState };
type ConversationClosedNotify = { conversationId: string; state: ConversationState };
type ConversationState = {
  conversationId: string;
  subject: string;
  status: ConversationStatus;
  customerActorId: string;
  agentActorId?: string;
  lastMessageSeq: number;
  lastMessageAtUnixMs?: number;
  idleDeadlineUnixMs?: number;
};
type ChatMessage = {
  conversationId: string;
  messageSeq: number;
  senderActorId: string;
  text: string;
  sentAtUnixMs: number;
};

const SupportChatRoles = {
  Agent: 'Agent',
  Customer: 'Customer'
} as const;

const ConversationStatuses = {
  WaitingForAgent: 'WaitingForAgent',
  Active: 'Active',
  WaitingForClose: 'WaitingForClose',
  Closed: 'Closed'
} as const;

const PacketNames = {
  authenticateReq: 'AuthenticateReq',
  authenticateRes: 'AuthenticateRes',
  authenticateUserReq: 'AuthenticateUserReq',
  authenticateUserRes: 'AuthenticateUserRes',
  openConversationApiReq: 'OpenConversationApiReq',
  openConversationApiRes: 'OpenConversationApiRes',
  allocateConversationReq: 'AllocateConversationReq',
  allocateConversationRes: 'AllocateConversationRes',
  assignAgentReq: 'AssignAgentReq',
  assignAgentRes: 'AssignAgentRes',
  openConversationReq: 'OpenConversationReq',
  openConversationRes: 'OpenConversationRes',
  setAgentAvailableReq: 'SetAgentAvailableReq',
  setAgentAvailableRes: 'SetAgentAvailableRes',
  joinConversationReq: 'JoinConversationReq',
  joinConversationRes: 'JoinConversationRes',
  sendChatMessageReq: 'SendChatMessageReq',
  sendChatMessageRes: 'SendChatMessageRes',
  setTypingReq: 'SetTypingReq',
  closeConversationReq: 'CloseConversationReq',
  closeConversationRes: 'CloseConversationRes',
  participantJoinedNotify: 'ParticipantJoinedNotify',
  conversationAssignedNotify: 'ConversationAssignedNotify',
  chatMessageNotify: 'ChatMessageNotify',
  typingChangedNotify: 'TypingChangedNotify',
  conversationIdleNotify: 'ConversationIdleNotify',
  conversationClosedNotify: 'ConversationClosedNotify'
} as const;

function authenticate(accessToken: string): AuthenticateReq {
  return new AuthenticateReq(accessToken);
}

function authenticateUser(accessToken: string): AuthenticateUserReq {
  return new AuthenticateUserReq(accessToken);
}

function openConversation(subject: string): OpenConversationReq {
  return new OpenConversationReq(subject);
}

function allocateConversation(customerActorId: string, customerDisplayName: string, subject: string): AllocateConversationReq {
  return new AllocateConversationReq(customerActorId, customerDisplayName, subject);
}

function assignAgent(conversationId: string, agentActorId: string): AssignAgentReq {
  return new AssignAgentReq(conversationId, agentActorId);
}

function setAgentAvailable(isAvailable: boolean): SetAgentAvailableReq {
  return new SetAgentAvailableReq(isAvailable);
}

function joinConversation(conversationId: string): JoinConversationReq {
  return new JoinConversationReq(conversationId);
}

function sendChatMessage(conversationId: string, text: string): SendChatMessageReq {
  return new SendChatMessageReq(conversationId, text);
}

function setTyping(conversationId: string, isTyping: boolean): SetTypingReq {
  return new SetTypingReq(conversationId, isTyping);
}

function closeConversation(conversationId: string, reason?: string): CloseConversationReq {
  return new CloseConversationReq(conversationId, reason);
}

export {
  PacketNames,
  AuthenticateReq,
  AuthenticateUserReq,
  OpenConversationApiReq,
  AllocateConversationReq,
  AssignAgentReq,
  OpenConversationReq,
  SetAgentAvailableReq,
  JoinConversationReq,
  SendChatMessageReq,
  SetTypingReq,
  CloseConversationReq,
  SupportChatRoles,
  ConversationStatuses,
  authenticate,
  authenticateUser,
  openConversation,
  allocateConversation,
  assignAgent,
  setAgentAvailable,
  joinConversation,
  sendChatMessage,
  setTyping,
  closeConversation
};

export type {
  SupportRole,
  ConversationStatus,
  AuthenticateRes,
  AuthenticateUserRes,
  OpenConversationApiRes,
  AllocateConversationRes,
  AssignAgentRes,
  OpenConversationRes,
  SetAgentAvailableRes,
  JoinConversationRes,
  SendChatMessageRes,
  CloseConversationRes,
  ParticipantJoinedNotify,
  ConversationAssignedNotify,
  ChatMessageNotify,
  TypingChangedNotify,
  ConversationIdleNotify,
  ConversationClosedNotify,
  ConversationState,
  ChatMessage
};

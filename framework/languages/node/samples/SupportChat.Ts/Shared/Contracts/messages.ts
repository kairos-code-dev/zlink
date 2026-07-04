type SupportRole = 'Agent' | 'Customer';
type ConversationStatus = 'WaitingForAgent' | 'Active' | 'WaitingForClose' | 'Closed';

type AuthenticateReq = { accessToken: string; packetName(): string };
type AuthenticateRes = { actorId: string; displayName: string; role: SupportRole };
type AuthenticateUserReq = { accessToken: string; packetName(): string };
type AuthenticateUserRes = {
  accepted: boolean;
  actorId?: string;
  displayName?: string;
  role?: SupportRole;
  reason?: string;
};
type OpenConversationApiReq = { customerActorId: string; customerDisplayName: string; subject: string; packetName(): string };
type OpenConversationApiRes = { conversationId: string; status: ConversationStatus };
type AllocateConversationReq = { customerActorId: string; customerDisplayName: string; subject: string; packetName(): string };
type AllocateConversationRes = { conversationId: string; status: ConversationStatus };
type AssignAgentReq = { conversationId: string; agentActorId: string; packetName(): string };
type AssignAgentRes = { conversationId: string; assigned: boolean };
type OpenConversationReq = { subject: string; packetName(): string };
type OpenConversationRes = { conversationId: string; state: ConversationState };
type SetAgentAvailableReq = { isAvailable: boolean; packetName(): string };
type SetAgentAvailableRes = { isAvailable: boolean };
type JoinConversationReq = { conversationId: string; packetName(): string };
type JoinConversationRes = { state: ConversationState };
type SendChatMessageReq = { conversationId: string; text: string; packetName(): string };
type SendChatMessageRes = { message: ChatMessage; state: ConversationState };
type SetTypingReq = { conversationId: string; isTyping: boolean; packetName(): string };
type CloseConversationReq = { conversationId: string; reason?: string; packetName(): string };
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
  return { accessToken, packetName: () => PacketNames.authenticateReq };
}

function authenticateUser(accessToken: string): AuthenticateUserReq {
  return { accessToken, packetName: () => PacketNames.authenticateUserReq };
}

function openConversation(subject: string): OpenConversationReq {
  return { subject, packetName: () => PacketNames.openConversationReq };
}

function allocateConversation(customerActorId: string, customerDisplayName: string, subject: string): AllocateConversationReq {
  return { customerActorId, customerDisplayName, subject, packetName: () => PacketNames.allocateConversationReq };
}

function assignAgent(conversationId: string, agentActorId: string): AssignAgentReq {
  return { conversationId, agentActorId, packetName: () => PacketNames.assignAgentReq };
}

function setAgentAvailable(isAvailable: boolean): SetAgentAvailableReq {
  return { isAvailable, packetName: () => PacketNames.setAgentAvailableReq };
}

function joinConversation(conversationId: string): JoinConversationReq {
  return { conversationId, packetName: () => PacketNames.joinConversationReq };
}

function sendChatMessage(conversationId: string, text: string): SendChatMessageReq {
  return { conversationId, text, packetName: () => PacketNames.sendChatMessageReq };
}

function setTyping(conversationId: string, isTyping: boolean): SetTypingReq {
  return { conversationId, isTyping, packetName: () => PacketNames.setTypingReq };
}

function closeConversation(conversationId: string, reason?: string): CloseConversationReq {
  return { conversationId, reason, packetName: () => PacketNames.closeConversationReq };
}

export {
  PacketNames,
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
  AuthenticateReq,
  AuthenticateRes,
  AuthenticateUserReq,
  AuthenticateUserRes,
  OpenConversationApiReq,
  OpenConversationApiRes,
  AllocateConversationReq,
  AllocateConversationRes,
  AssignAgentReq,
  AssignAgentRes,
  OpenConversationReq,
  OpenConversationRes,
  SetAgentAvailableReq,
  SetAgentAvailableRes,
  JoinConversationReq,
  JoinConversationRes,
  SendChatMessageReq,
  SendChatMessageRes,
  SetTypingReq,
  CloseConversationReq,
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

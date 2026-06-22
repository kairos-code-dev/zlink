const PacketNames = Object.freeze({
  authenticateReq: 'AuthenticateReq',
  authenticateRes: 'AuthenticateRes',
  authenticateUserReq: 'AuthenticateUserReq',
  ensureSupportUserActorReq: 'EnsureSupportUserActorReq',
  openConversationApiReq: 'OpenConversationApiReq',
  allocateConversationReq: 'AllocateConversationReq',
  assignAgentReq: 'AssignAgentReq',
  openConversationReq: 'OpenConversationReq',
  setAgentAvailableReq: 'SetAgentAvailableReq',
  joinConversationReq: 'JoinConversationReq',
  sendChatMessageReq: 'SendChatMessageReq',
  setTypingReq: 'SetTypingReq',
  closeConversationReq: 'CloseConversationReq',
  supportNotificationsReq: 'SupportNotificationsReq',
  participantJoinedNotify: 'ParticipantJoinedNotify',
  conversationAssignedNotify: 'ConversationAssignedNotify',
  chatMessageNotify: 'ChatMessageNotify',
  typingChangedNotify: 'TypingChangedNotify',
  conversationIdleNotify: 'ConversationIdleNotify',
  conversationClosedNotify: 'ConversationClosedNotify'
});

const SupportChatRoles = Object.freeze({
  customer: 'Customer',
  agent: 'Agent'
});

export enum ConversationStatus {
  WaitingForAgent = 'WaitingForAgent',
  Active = 'Active',
  WaitingForClose = 'WaitingForClose',
  Closed = 'Closed'
}

const SupportSampleUsers = Object.freeze({
  customer1: 'customer-1',
  customer2: 'customer-2',
  customer3: 'customer-3',
  agent1: 'agent-1',
  agent2: 'agent-2'
});

// --- client stream authentication ---

export class AuthenticateReq {
  accessToken: string;

  constructor(accessToken: string) {
    this.accessToken = accessToken;
  }
}

export interface AuthenticateRes {
  actorId: string;
  displayName: string;
  role: string;
}

// --- API authentication and actor preparation ---

export class AuthenticateUserReq {
  accessToken: string;

  constructor(accessToken: string) {
    this.accessToken = accessToken;
  }
}

export interface AuthenticateUserRes {
  accepted: boolean;
  actorId: string | null;
  displayName: string | null;
  role: string | null;
  reason: string | null;
}

export class EnsureSupportUserActorReq {
  actorId: string;
  displayName: string;
  role: string;

  constructor(actorId: string, displayName: string, role: string) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.role = role;
  }
}

export interface ActorRefSnapshot {
  nodeRid: string;
  actorId: string;
  generation: number;
}

export interface EnsureSupportUserActorRes {
  actor: ActorRefSnapshot;
}

// --- API orchestration and Support allocation ---

export class OpenConversationApiReq {
  customerActorId: string;
  customerDisplayName: string;
  subject: string;

  constructor(customerActorId: string, customerDisplayName: string, subject: string) {
    this.customerActorId = customerActorId;
    this.customerDisplayName = customerDisplayName;
    this.subject = subject;
  }
}

export interface OpenConversationApiRes {
  conversationId: string;
  status: string;
}

export class AllocateConversationReq {
  customerActorId: string;
  customerDisplayName: string;
  subject: string;

  constructor(customerActorId: string, customerDisplayName: string, subject: string) {
    this.customerActorId = customerActorId;
    this.customerDisplayName = customerDisplayName;
    this.subject = subject;
  }
}

export interface AllocateConversationRes {
  conversationId: string;
  status: string;
}

export class AssignAgentReq {
  conversationId: string;
  requestedAgentActorId: string | null;

  constructor(conversationId: string, requestedAgentActorId: string | null) {
    this.conversationId = conversationId;
    this.requestedAgentActorId = requestedAgentActorId;
  }
}

export interface AssignAgentRes {
  conversationId: string;
  status: string;
  agentActorId: string | null;
}

// --- client stream request/response ---

export class OpenConversationReq {
  subject: string;
  actorId?: string;
  displayName?: string;

  constructor(subject: string) {
    this.subject = subject;
  }
}

export interface OpenConversationRes {
  conversationId: string;
  state: ConversationState;
}

export class SetAgentAvailableReq {
  isAvailable: boolean;
  actorId?: string;
  displayName?: string;

  constructor(isAvailable: boolean) {
    this.isAvailable = isAvailable;
  }
}

export interface SetAgentAvailableRes {
  isAvailable: boolean;
}

export class JoinConversationReq {
  conversationId: string;
  actorId?: string;
  displayName?: string;

  constructor(conversationId: string) {
    this.conversationId = conversationId;
  }
}

export interface JoinConversationRes {
  state: ConversationState;
}

export class SendChatMessageReq {
  conversationId: string;
  text: string;
  actorId?: string;
  displayName?: string;

  constructor(conversationId: string, text: string) {
    this.conversationId = conversationId;
    this.text = text;
  }
}

export interface SendChatMessageRes {
  message: ChatMessage;
  state: ConversationState;
}

export class SetTypingReq {
  conversationId: string;
  isTyping: boolean;
  actorId?: string;
  displayName?: string;

  constructor(conversationId: string, isTyping: boolean) {
    this.conversationId = conversationId;
    this.isTyping = isTyping;
  }
}

export interface SetTypingRes {
  state: ConversationState;
}

export class CloseConversationReq {
  conversationId: string;
  reason: string | null;
  actorId?: string;
  displayName?: string;

  constructor(conversationId: string, reason: string | null) {
    this.conversationId = conversationId;
    this.reason = reason;
  }
}

export interface CloseConversationRes {
  state: ConversationState;
}

// --- server push messages ---

export interface ParticipantJoinedNotify {
  conversationId: string;
  actorId: string;
  role: string;
  state: ConversationState;
}

export interface ConversationAssignedNotify {
  conversationId: string;
  state: ConversationState;
}

export interface ChatMessageNotify {
  conversationId: string;
  message: ChatMessage;
  state: ConversationState;
}

export interface TypingChangedNotify {
  conversationId: string;
  actorId: string;
  isTyping: boolean;
  state: ConversationState;
}

export interface ConversationIdleNotify {
  conversationId: string;
  state: ConversationState;
}

export interface ConversationClosedNotify {
  conversationId: string;
  state: ConversationState;
}

// --- state model ---

export interface ConversationState {
  conversationId: string;
  subject: string;
  status: string;
  customerActorId: string;
  agentActorId: string | null;
  lastMessageSeq: number;
  lastMessageAtUnixMs: number | null;
  idleDeadlineUnixMs: number | null;
}

export interface ChatMessage {
  conversationId: string;
  messageSeq: number;
  senderActorId: string;
  text: string;
  sentAtUnixMs: number;
}

// --- notification delivery channel framing ---

export class SupportNotificationsReq {
  afterSeq: number;
  actorId?: string;
  displayName?: string;

  constructor(afterSeq: number) {
    this.afterSeq = afterSeq;
  }
}

export interface SupportNotificationFrame {
  seq: number;
  actorId: string;
  packetName: string;
  payload: unknown;
}

export interface SupportNotificationBatch {
  nextSeq: number;
  delivered: SupportNotificationFrame[];
}

export interface UserIdentity {
  actorId: string;
  displayName: string;
}

// --- factory helpers ---

function authenticateReq(accessToken: string): AuthenticateReq {
  return new AuthenticateReq(accessToken);
}

function authenticateRes(actorId: string, displayName: string, role: string): AuthenticateRes {
  return { actorId, displayName, role };
}

function authenticateUserReq(accessToken: string): AuthenticateUserReq {
  return new AuthenticateUserReq(accessToken);
}

function authenticateUserAccepted(actorId: string, displayName: string, role: string): AuthenticateUserRes {
  return { accepted: true, actorId, displayName, role, reason: null };
}

function authenticateUserRejected(reason: string): AuthenticateUserRes {
  return { accepted: false, actorId: null, displayName: null, role: null, reason };
}

function ensureSupportUserActorReq(actorId: string, displayName: string, role: string): EnsureSupportUserActorReq {
  return new EnsureSupportUserActorReq(actorId, displayName, role);
}

function ensureSupportUserActorRes(actor: ActorRefSnapshot): EnsureSupportUserActorRes {
  return { actor };
}

function openConversationApiReq(customerActorId: string, customerDisplayName: string, subject: string): OpenConversationApiReq {
  return new OpenConversationApiReq(customerActorId, customerDisplayName, subject);
}

function openConversationApiRes(conversationId: string, status: string): OpenConversationApiRes {
  return { conversationId, status };
}

function allocateConversationReq(customerActorId: string, customerDisplayName: string, subject: string): AllocateConversationReq {
  return new AllocateConversationReq(customerActorId, customerDisplayName, subject);
}

function allocateConversationRes(conversationId: string, status: string): AllocateConversationRes {
  return { conversationId, status };
}

function assignAgentReq(conversationId: string, requestedAgentActorId: string | null = null): AssignAgentReq {
  return new AssignAgentReq(conversationId, requestedAgentActorId);
}

function assignAgentRes(conversationId: string, status: string, agentActorId: string | null): AssignAgentRes {
  return { conversationId, status, agentActorId };
}

function openConversationReq(subject: string): OpenConversationReq {
  return new OpenConversationReq(subject);
}

function openConversationRes(conversationId: string, state: ConversationState): OpenConversationRes {
  return { conversationId, state };
}

function setAgentAvailableReq(isAvailable: boolean): SetAgentAvailableReq {
  return new SetAgentAvailableReq(isAvailable);
}

function setAgentAvailableRes(isAvailable: boolean): SetAgentAvailableRes {
  return { isAvailable };
}

function joinConversationReq(conversationId: string): JoinConversationReq {
  return new JoinConversationReq(conversationId);
}

function joinConversationRes(state: ConversationState): JoinConversationRes {
  return { state };
}

function sendChatMessageReq(conversationId: string, text: string): SendChatMessageReq {
  return new SendChatMessageReq(conversationId, text);
}

function sendChatMessageRes(message: ChatMessage, state: ConversationState): SendChatMessageRes {
  return { message, state };
}

function setTypingReq(conversationId: string, isTyping: boolean): SetTypingReq {
  return new SetTypingReq(conversationId, isTyping);
}

function setTypingRes(state: ConversationState): SetTypingRes {
  return { state };
}

function closeConversationReq(conversationId: string, reason: string | null = null): CloseConversationReq {
  return new CloseConversationReq(conversationId, reason);
}

function closeConversationRes(state: ConversationState): CloseConversationRes {
  return { state };
}

function supportNotificationsReq(afterSeq: number): SupportNotificationsReq {
  return new SupportNotificationsReq(afterSeq);
}

function withUserIdentity<TRequest extends object>(
  request: TRequest,
  actorId: string,
  displayName: string
): TRequest & UserIdentity {
  const value = request as TRequest & Partial<UserIdentity>;
  value.actorId = actorId;
  value.displayName = displayName;
  return value as TRequest & UserIdentity;
}

export {
  ConversationStatus as ConversationStatuses,
  PacketNames,
  SupportChatRoles,
  SupportSampleUsers,
  allocateConversationReq,
  allocateConversationRes,
  assignAgentReq,
  assignAgentRes,
  authenticateReq,
  authenticateRes,
  authenticateUserAccepted,
  authenticateUserRejected,
  authenticateUserReq,
  closeConversationReq,
  closeConversationRes,
  ensureSupportUserActorReq,
  ensureSupportUserActorRes,
  joinConversationReq,
  joinConversationRes,
  openConversationApiReq,
  openConversationApiRes,
  openConversationReq,
  openConversationRes,
  sendChatMessageReq,
  sendChatMessageRes,
  setAgentAvailableReq,
  setAgentAvailableRes,
  setTypingReq,
  setTypingRes,
  supportNotificationsReq,
  withUserIdentity
};

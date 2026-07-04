import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname } from 'node:path';
import { ConversationStatuses, SupportChatRoles } from '../../Shared/Contracts/messages';
import type {
  AuthenticateRes,
  ChatMessage,
  ConversationState,
  ConversationStatus,
  SupportRole
} from '../../Shared/Contracts/messages';

type PersistedConversation = ConversationState & {
  idleArmedAtUnixMs?: number;
};

type SupportChatSnapshot = {
  nextConversationId: number;
  agentsAvailable: Record<string, boolean>;
  conversations: Record<string, PersistedConversation>;
  notifications: Record<string, Array<{ name: string; payload: unknown }>>;
};

const initialSnapshot = (): SupportChatSnapshot => ({
  nextConversationId: 1,
  agentsAvailable: {},
  conversations: {},
  notifications: {}
});

class SupportChatStateStore {
  constructor(private readonly fileName: string) {}

  authenticate(accessToken: string): AuthenticateRes {
    const role: SupportRole = accessToken.startsWith('agent-')
      ? SupportChatRoles.Agent
      : SupportChatRoles.Customer;
    return {
      actorId: accessToken,
      displayName: role === SupportChatRoles.Agent ? `Agent ${accessToken}` : `Customer ${accessToken}`,
      role
    };
  }

  setAgentAvailable(agentActorId: string, isAvailable: boolean): boolean {
    const snapshot = this.load();
    snapshot.agentsAvailable[agentActorId] = isAvailable;
    this.save(snapshot);
    return isAvailable;
  }

  openConversation(customerActorId: string, subject: string): { conversationId: string; state: ConversationState } {
    const snapshot = this.load();
    const conversationId = `support-${snapshot.nextConversationId++}`;
    const agentActorId = firstAvailableAgent(snapshot);
    const state: PersistedConversation = {
      conversationId,
      subject,
      status: ConversationStatuses.WaitingForAgent,
      customerActorId,
      agentActorId,
      lastMessageSeq: 0
    };
    snapshot.conversations[conversationId] = state;
    if (agentActorId !== undefined) {
      push(snapshot, agentActorId, 'ConversationAssignedNotify', { conversationId, state: cloneState(state) });
    }
    this.save(snapshot);
    return { conversationId, state: cloneState(state) };
  }

  join(conversationId: string, actorId: string): ConversationState {
    const snapshot = this.load();
    const state = requireConversation(snapshot, conversationId);
    if (state.status !== ConversationStatuses.Closed && actorId === state.agentActorId) {
      state.status = ConversationStatuses.Active;
      push(snapshot, state.customerActorId, 'ParticipantJoinedNotify', {
        conversationId,
        actorId,
        role: SupportChatRoles.Agent,
        state: cloneState(state)
      });
    }
    this.save(snapshot);
    return cloneState(state);
  }

  sendChat(conversationId: string, actorId: string, text: string): { message: ChatMessage; state: ConversationState } {
    const snapshot = this.load();
    const state = requireConversation(snapshot, conversationId);
    if (state.status === ConversationStatuses.Closed || state.status === ConversationStatuses.WaitingForClose) {
      throw new Error('Closed conversation must reject follow-up messages.');
    }
    const message: ChatMessage = {
      conversationId,
      messageSeq: state.lastMessageSeq + 1,
      senderActorId: actorId,
      text,
      sentAtUnixMs: Date.now()
    };
    state.lastMessageSeq = message.messageSeq;
    state.lastMessageAtUnixMs = message.sentAtUnixMs;
    for (const participant of participants(state)) {
      if (participant !== actorId) {
        push(snapshot, participant, 'ChatMessageNotify', { conversationId, message, state: cloneState(state) });
      }
    }
    this.save(snapshot);
    return { message, state: cloneState(state) };
  }

  setTyping(conversationId: string, actorId: string, isTyping: boolean): void {
    const snapshot = this.load();
    const state = requireConversation(snapshot, conversationId);
    if (state.status === ConversationStatuses.Closed || state.status === ConversationStatuses.WaitingForClose) {
      this.save(snapshot);
      return;
    }
    for (const participant of participants(state)) {
      if (participant !== actorId) {
        push(snapshot, participant, 'TypingChangedNotify', { conversationId, actorId, isTyping, state: cloneState(state) });
      }
    }
    this.save(snapshot);
  }

  close(conversationId: string): ConversationState {
    const snapshot = this.load();
    const state = requireConversation(snapshot, conversationId);
    if (state.status === ConversationStatuses.Closed) {
      throw new Error('Closed conversation must reject a duplicate close.');
    }
    state.status = ConversationStatuses.Closed;
    delete state.idleDeadlineUnixMs;
    for (const participant of participants(state)) {
      push(snapshot, participant, 'ConversationClosedNotify', { conversationId, state: cloneState(state) });
    }
    this.save(snapshot);
    return cloneState(state);
  }

  armIdle(conversationId: string): void {
    const snapshot = this.load();
    const state = requireConversation(snapshot, conversationId);
    if (state.status !== ConversationStatuses.Active) {
      this.save(snapshot);
      return;
    }
    state.status = ConversationStatuses.WaitingForClose;
    state.idleDeadlineUnixMs = Date.now();
    state.idleArmedAtUnixMs = Date.now();
    for (const participant of participants(state)) {
      push(snapshot, participant, 'ConversationIdleNotify', { conversationId, state: cloneState(state) });
    }
    state.status = ConversationStatuses.Closed;
    delete state.idleDeadlineUnixMs;
    for (const participant of participants(state)) {
      push(snapshot, participant, 'ConversationClosedNotify', { conversationId, state: cloneState(state) });
    }
    this.save(snapshot);
  }

  takeNotification(actorId: string, name: string, conversationId?: string): { name: string; payload: unknown } | undefined {
    const snapshot = this.load();
    const queue = snapshot.notifications[actorId] ?? [];
    const index = queue.findIndex((entry) => {
      const payload = entry.payload as { conversationId?: string };
      return entry.name === name && (conversationId === undefined || payload.conversationId === conversationId);
    });
    if (index < 0) {
      return undefined;
    }
    const [entry] = queue.splice(index, 1);
    snapshot.notifications[actorId] = queue;
    this.save(snapshot);
    return entry;
  }

  evidence(): string[] {
    const snapshot = this.load();
    return Object.values(snapshot.conversations).map((state) => {
      return `conversation=${state.conversationId} status=${state.status} seq=${state.lastMessageSeq}`;
    });
  }

  private load(): SupportChatSnapshot {
    try {
      return JSON.parse(readFileSync(this.fileName, 'utf8')) as SupportChatSnapshot;
    } catch {
      return initialSnapshot();
    }
  }

  private save(snapshot: SupportChatSnapshot): void {
    mkdirSync(dirname(this.fileName), { recursive: true });
    writeFileSync(this.fileName, JSON.stringify(snapshot, null, 2));
  }
}

function firstAvailableAgent(snapshot: SupportChatSnapshot): string | undefined {
  return Object.entries(snapshot.agentsAvailable).find(([, available]) => available)?.[0];
}

function requireConversation(snapshot: SupportChatSnapshot, conversationId: string): PersistedConversation {
  const state = snapshot.conversations[conversationId];
  if (state === undefined) {
    throw new Error(`Unknown conversation '${conversationId}'.`);
  }
  return state;
}

function participants(state: ConversationState): string[] {
  return [state.customerActorId, state.agentActorId].filter((value): value is string => value !== undefined);
}

function push(snapshot: SupportChatSnapshot, actorId: string, name: string, payload: unknown): void {
  snapshot.notifications[actorId] ??= [];
  snapshot.notifications[actorId].push({ name, payload });
}

function cloneState(state: ConversationState): ConversationState {
  return {
    conversationId: state.conversationId,
    subject: state.subject,
    status: state.status as ConversationStatus,
    customerActorId: state.customerActorId,
    agentActorId: state.agentActorId,
    lastMessageSeq: state.lastMessageSeq,
    lastMessageAtUnixMs: state.lastMessageAtUnixMs,
    idleDeadlineUnixMs: state.idleDeadlineUnixMs
  };
}

export { SupportChatStateStore };

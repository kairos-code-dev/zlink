import http from 'node:http';
import { ConversationStatuses, SupportChatRoles } from '../Shared/Contracts/messages';
import { SampleTimings } from './Configuration/sample-names';
import type {
  AuthenticateRes,
  ChatMessageNotify,
  CloseConversationRes,
  ConversationAssignedNotify,
  ConversationClosedNotify,
  ConversationIdleNotify,
  JoinConversationRes,
  OpenConversationRes,
  ParticipantJoinedNotify,
  SendChatMessageRes,
  SetAgentAvailableRes,
  TypingChangedNotify
} from '../Shared/Contracts/messages';

class SupportChatClientScenario {
  constructor(private readonly baseUrl: string) {}

  async run(): Promise<void> {
    const agentAuth = await this.post<AuthenticateRes>('/auth', { accessToken: 'agent-1' });
    ensure(() => agentAuth.actorId === 'agent-1');
    ensure(() => agentAuth.role === SupportChatRoles.Agent);
    ensure(() => (agentAuth.displayName ?? '').length > 0);
    ensure((await this.setAvailable('agent-1', true)).isAvailable);

    const customer1 = await this.post<AuthenticateRes>('/auth', { accessToken: 'customer-1' });
    ensure(() => customer1.actorId === 'customer-1');
    const opened1 = await this.open('customer-1', 'checkout payment failed');
    const cid1 = opened1.conversationId;
    ensure(() => opened1.state.status === ConversationStatuses.WaitingForAgent);
    const assigned1 = await this.take<ConversationAssignedNotify>('agent-1', 'ConversationAssignedNotify', cid1);
    ensure(() => assigned1.payload.conversationId === cid1);

    const agentJoin1 = await this.join(cid1, 'agent-1');
    ensure(() => agentJoin1.state.status === ConversationStatuses.Active);
    ensure(() => agentJoin1.state.agentActorId === 'agent-1');
    ensure(() => agentJoin1.state.subject === 'checkout payment failed');
    const joined1 = await this.take<ParticipantJoinedNotify>('customer-1', 'ParticipantJoinedNotify', cid1);
    ensure(() => joined1.payload.actorId === 'agent-1');
    ensure(() => joined1.payload.state.status === ConversationStatuses.Active);

    const greet1 = await this.send(cid1, 'agent-1', 'How can I help?');
    ensure(() => greet1.message.messageSeq === 1);
    const greeting1 = await this.take<ChatMessageNotify>('customer-1', 'ChatMessageNotify', cid1);
    ensure(() => greeting1.payload.message.messageSeq === 1);
    const reply1 = await this.send(cid1, 'customer-1', 'Payment keeps failing.');
    ensure(() => reply1.message.messageSeq === 2);
    const reply1Push = await this.take<ChatMessageNotify>('agent-1', 'ChatMessageNotify', cid1);
    ensure(() => reply1Push.payload.message.messageSeq === 2);

    const customer2 = await this.post<AuthenticateRes>('/auth', { accessToken: 'customer-2' });
    ensure(() => customer2.actorId === 'customer-2');
    const opened2 = await this.open('customer-2', 'cannot log in');
    const cid2 = opened2.conversationId;
    ensure(() => cid2 !== cid1);
    ensure(() => opened2.state.status === ConversationStatuses.WaitingForAgent);
    const assigned2 = await this.take<ConversationAssignedNotify>('agent-1', 'ConversationAssignedNotify', cid2);
    ensure(() => assigned2.payload.conversationId === cid2);
    const agentJoin2 = await this.join(cid2, 'agent-1');
    ensure(() => agentJoin2.state.status === ConversationStatuses.Active);
    ensure(() => agentJoin2.state.subject === 'cannot log in');
    const joined2 = await this.take<ParticipantJoinedNotify>('customer-2', 'ParticipantJoinedNotify', cid2);
    ensure(() => joined2.payload.conversationId === cid2);

    const greet2 = await this.send(cid2, 'agent-1', 'Let me check your account.');
    ensure(() => greet2.message.messageSeq === 1);
    const greeting2 = await this.take<ChatMessageNotify>('customer-2', 'ChatMessageNotify', cid2);
    ensure(() => greeting2.payload.message.messageSeq === 1);

    await this.post('/conversation/typing', { conversationId: cid1, actorId: 'agent-1', isTyping: true });
    const typing1 = await this.take<TypingChangedNotify>('customer-1', 'TypingChangedNotify', cid1);
    ensure(() => typing1.payload.actorId === 'agent-1');
    ensure(() => typing1.payload.isTyping);

    const customerRejoin1 = await this.join(cid1, 'customer-1');
    ensure(() => customerRejoin1.state.subject === 'checkout payment failed');
    ensure(() => customerRejoin1.state.status === ConversationStatuses.Active);
    ensure(() => customerRejoin1.state.lastMessageSeq === 2);

    ensure((await this.setAvailable('agent-1', true)).isAvailable);
    const reconnectedRoom1 = await this.join(cid1, 'agent-1');
    const reconnectedRoom2 = await this.join(cid2, 'agent-1');
    ensure(() => reconnectedRoom1.state.subject === 'checkout payment failed');
    ensure(() => reconnectedRoom2.state.subject === 'cannot log in');

    const closed2 = await this.close(cid2);
    ensure(() => closed2.state.status === ConversationStatuses.Closed);
    const closed2ForAgent = await this.take<ConversationClosedNotify>('agent-1', 'ConversationClosedNotify', cid2);
    ensure(() => closed2ForAgent.payload.state.status === ConversationStatuses.Closed);
    await expectFailure(() => this.close(cid2), 'Closed conversation must reject a duplicate close.');

    await this.post('/conversation/idle', { conversationId: cid1 });
    const idle1ForCustomer = await this.take<ConversationIdleNotify>('customer-1', 'ConversationIdleNotify', cid1);
    const idle1ForAgent = await this.take<ConversationIdleNotify>('agent-1', 'ConversationIdleNotify', cid1);
    const closed1ForCustomer = await this.take<ConversationClosedNotify>('customer-1', 'ConversationClosedNotify', cid1);
    const closed1ForAgent = await this.take<ConversationClosedNotify>('agent-1', 'ConversationClosedNotify', cid1);
    ensure(() => idle1ForCustomer.payload.state.status === ConversationStatuses.WaitingForClose);
    ensure(() => idle1ForAgent.payload.state.status === ConversationStatuses.WaitingForClose);
    ensure(() => closed1ForCustomer.payload.state.status === ConversationStatuses.Closed);
    ensure(() => closed1ForAgent.payload.state.status === ConversationStatuses.Closed);

    await expectFailure(() => this.send(cid1, 'customer-1', 'are you there?'), 'Closed conversation must reject follow-up messages.');
    await this.post('/conversation/typing', { conversationId: cid1, actorId: 'customer-1', isTyping: true });
    await expectNoPush(() => this.take<TypingChangedNotify>('agent-1', 'TypingChangedNotify', cid1));
    console.log('supportchat-closed-typing-ignore=verified');

    ensure(!(await this.setAvailable('agent-1', false)).isAvailable);
    const waitingCustomer = await this.post<AuthenticateRes>('/auth', { accessToken: 'customer-3' });
    ensure(() => waitingCustomer.actorId === 'customer-3');
    await expectFailure(
      () => this.post('/agent/available', { actorId: 'customer-3', isAvailable: true }),
      'Customer must not set agent availability.'
    );
    const noAgentOpen = await this.open('customer-3', 'agent unavailable');
    ensure(() => noAgentOpen.state.status === ConversationStatuses.WaitingForAgent);
    ensure(() => noAgentOpen.state.subject === 'agent unavailable');
    await expectNoPush(() => this.take<ConversationClosedNotify>('customer-3', 'ConversationClosedNotify', noAgentOpen.conversationId));
  }

  private setAvailable(actorId: string, isAvailable: boolean): Promise<SetAgentAvailableRes> {
    return this.post('/agent/available', { actorId, isAvailable });
  }

  private open(actorId: string, subject: string): Promise<OpenConversationRes> {
    return this.post('/conversation/open', { actorId, subject });
  }

  private join(conversationId: string, actorId: string): Promise<JoinConversationRes> {
    return this.post('/conversation/join', { conversationId, actorId });
  }

  private send(conversationId: string, actorId: string, text: string): Promise<SendChatMessageRes> {
    return this.post('/conversation/send', { conversationId, actorId, text });
  }

  private close(conversationId: string): Promise<CloseConversationRes> {
    return this.post('/conversation/close', { conversationId });
  }

  private async take<TPayload>(actorId: string, name: string, conversationId?: string): Promise<{ name: string; payload: TPayload }> {
    const url = `/notifications/take?actorId=${encodeURIComponent(actorId)}&name=${encodeURIComponent(name)}${conversationId === undefined ? '' : `&conversationId=${encodeURIComponent(conversationId)}`}`;
    const message = await this.get<{ name: string; payload: TPayload } | null>(url);
    if (message === null) {
      throw new Error(`Missing ${name} for ${actorId}.`);
    }
    return message;
  }

  private get<TResponse>(path: string): Promise<TResponse> {
    return request<TResponse>('GET', `${this.baseUrl}${path}`);
  }

  private post<TResponse>(path: string, bodyValue: unknown): Promise<TResponse> {
    return request<TResponse>('POST', `${this.baseUrl}${path}`, bodyValue);
  }
}

function request<TResponse>(method: string, url: string, bodyValue?: unknown): Promise<TResponse> {
  return new Promise((resolve, reject) => {
    const payload = bodyValue === undefined ? undefined : JSON.stringify(bodyValue);
    const req = http.request(url, {
      method,
      headers: payload === undefined
        ? undefined
        : {
          'content-type': 'application/json',
          'content-length': Buffer.byteLength(payload)
        },
      timeout: SampleTimings.clientTimeout
    }, (res) => {
      const chunks: Buffer[] = [];
      res.on('data', (chunk: Buffer) => chunks.push(chunk));
      res.on('end', () => {
        const text = Buffer.concat(chunks).toString('utf8');
        const value = text.length === 0 ? undefined : JSON.parse(text);
        if ((res.statusCode ?? 500) >= 400) {
          reject(new Error(value?.error ?? `HTTP ${res.statusCode}`));
          return;
        }
        resolve(value as TResponse);
      });
    });
    req.on('error', reject);
    if (payload !== undefined) {
      req.write(payload);
    }
    req.end();
  });
}

function ensure(condition: boolean | (() => boolean)): void {
  const ok = typeof condition === 'function' ? condition() : condition;
  if (!ok) {
    throw new Error('Ensure failed.');
  }
}

async function expectFailure(action: () => Promise<unknown>, message: string): Promise<void> {
  try {
    await action();
  } catch {
    return;
  }
  throw new Error(message);
}

async function expectNoPush(action: () => unknown): Promise<void> {
  try {
    await action();
  } catch {
    return;
  }
  throw new Error('Unexpected push was received.');
}

export { SupportChatClientScenario };

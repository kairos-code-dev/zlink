import {
  ConversationStatuses,
  SupportChatRoles,
  authenticateReq,
  closeConversationReq,
  openConversationReq,
  sendChatMessageReq,
  setAgentAvailableReq,
  setTypingReq,
  PacketNames,
  joinConversationReq
} from '../Shared/Contracts/messages';
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
  SetTypingRes,
  TypingChangedNotify
} from '../Shared/Contracts/messages';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

type SupportChatClients = {
  customer: ZlinkStreamConnector;
  agent: ZlinkStreamConnector;
  reconnectingCustomer: ZlinkStreamConnector;
  waitingCustomer: ZlinkStreamConnector;
  closingCustomer: ZlinkStreamConnector;
  closingAgent: ZlinkStreamConnector;
};

class SupportChatClientScenario {
  async run(clients: SupportChatClients, signal?: AbortSignal): Promise<void> {
    const { customer, agent, reconnectingCustomer, waitingCustomer, closingCustomer, closingAgent } = clients;

    // 1-3. Agent connects, authenticates, and registers availability.
    await agent.connect(signal);
    const agentAuth = await agent.request(authenticateReq('agent-1')).submit<AuthenticateRes>(signal);
    ensure(() => agentAuth.actorId === 'agent-1');
    ensure(() => agentAuth.role === SupportChatRoles.agent);

    const available = await agent.request(setAgentAvailableReq(true)).submit<SetAgentAvailableRes>(signal);
    ensure(() => available.isAvailable);

    // 4-8. Customer connects, opens a conversation; agent is assigned, both sides observe it.
    await customer.connect(signal);
    const customerAuth = await customer.request(authenticateReq('customer-1')).submit<AuthenticateRes>(signal);
    ensure(() => customerAuth.actorId === 'customer-1');
    ensure(() => customerAuth.role === SupportChatRoles.customer);

    const joinedForCustomer = customer.waitFor<ParticipantJoinedNotify>(PacketNames.participantJoinedNotify).submit(signal);
    const assignedForAgent = agent.waitFor<ConversationAssignedNotify>(PacketNames.conversationAssignedNotify).submit(signal);
    const opened = await customer.request(openConversationReq('checkout payment failed')).submit<OpenConversationRes>(signal);
    ensure(() => opened.state.customerActorId === customerAuth.actorId);
    ensure(() => opened.state.subject === 'checkout payment failed');
    ensure(() => opened.state.status === ConversationStatuses.WaitingForAgent || opened.state.status === ConversationStatuses.Active);

    const joined = await joinedForCustomer;
    ensure(() => joined.payload.actorId === agentAuth.actorId);
    ensure(() => joined.payload.conversationId === opened.conversationId);
    ensure(() => joined.payload.role === SupportChatRoles.agent);
    ensure(() => joined.payload.state.status === ConversationStatuses.Active);

    const assigned = await assignedForAgent;
    ensure(() => assigned.payload.conversationId === opened.conversationId);
    ensure(() => assigned.payload.state.agentActorId === agentAuth.actorId);

    // 9-10. Agent sends the greeting (MessageSeq 1); customer receives it as ChatMessageNotify.
    const agentMessageForCustomer = customer.waitFor<ChatMessageNotify>(PacketNames.chatMessageNotify).submit(signal);
    const sentByAgent = await agent.request(sendChatMessageReq(opened.conversationId, 'I can help with a new payment link.')).submit<SendChatMessageRes>(signal);
    ensure(() => sentByAgent.message.messageSeq === 1);
    ensure(() => sentByAgent.state.lastMessageSeq === 1);
    const agentMessagePush = await agentMessageForCustomer;
    ensure(() => agentMessagePush.payload.message.senderActorId === agentAuth.actorId);
    ensure(() => agentMessagePush.payload.message.text === 'I can help with a new payment link.');

    // 11-12. Customer replies (MessageSeq 2); agent receives it.
    const customerMessageForAgent = agent.waitFor<ChatMessageNotify>(PacketNames.chatMessageNotify).submit(signal);
    const sentByCustomer = await customer.request(sendChatMessageReq(opened.conversationId, 'I cannot complete payment.')).submit<SendChatMessageRes>(signal);
    ensure(() => sentByCustomer.message.messageSeq === 2);
    const customerMessagePush = await customerMessageForAgent;
    ensure(() => customerMessagePush.payload.message.senderActorId === customerAuth.actorId);
    ensure(() => customerMessagePush.payload.message.text === 'I cannot complete payment.');

    // 13-14. Typing change: agent types, customer receives TypingChangedNotify.
    const typingForCustomer = customer.waitFor<TypingChangedNotify>(PacketNames.typingChangedNotify).submit(signal);
    const typing = await agent.request(setTypingReq(opened.conversationId, true)).submit<SetTypingRes>(signal);
    ensure(() => typing.state.status === ConversationStatuses.Active);
    const typingPush = await typingForCustomer;
    ensure(() => typingPush.payload.actorId === agentAuth.actorId);
    ensure(() => typingPush.payload.isTyping);

    // 15-16. Reconnect the customer with the same token; JoinConversationReq returns current state.
    await reconnectingCustomer.connect(signal);
    const reconnected = await reconnectingCustomer.request(authenticateReq('customer-1')).submit<AuthenticateRes>(signal);
    ensure(() => reconnected.actorId === customerAuth.actorId);
    const rejoin = await reconnectingCustomer.request(joinConversationReq(opened.conversationId)).submit<JoinConversationRes>(signal);
    ensure(() => rejoin.state.conversationId === opened.conversationId);
    ensure(() => rejoin.state.subject === 'checkout payment failed');
    ensure(() => rejoin.state.customerActorId === customerAuth.actorId);

    // 17. Both clients receive the idle notify after the idle timeout elapses.
    const idleForCustomer = reconnectingCustomer.waitFor<ConversationIdleNotify>(PacketNames.conversationIdleNotify).timeout(SampleTimings.idleNotifyTimeout).submit(signal);
    const idleForAgent = agent.waitFor<ConversationIdleNotify>(PacketNames.conversationIdleNotify).timeout(SampleTimings.idleNotifyTimeout).submit(signal);
    const idleCustomerPush = await idleForCustomer;
    ensure(() => idleCustomerPush.payload.conversationId === opened.conversationId);
    ensure(() => idleCustomerPush.payload.state.status === ConversationStatuses.WaitingForClose);
    const idleAgentPush = await idleForAgent;
    ensure(() => idleAgentPush.payload.state.status === ConversationStatuses.WaitingForClose);

    // Explicit close: customer closes, agent receives ConversationClosedNotify.
    const closedForAgent = agent.waitFor<ConversationClosedNotify>(PacketNames.conversationClosedNotify).submit(signal);
    const closed = await reconnectingCustomer.request(closeConversationReq(opened.conversationId, 'resolved')).submit<CloseConversationRes>(signal);
    ensure(() => closed.state.status === ConversationStatuses.Closed);
    const closedAgentPush = await closedForAgent;
    ensure(() => closedAgentPush.payload.conversationId === opened.conversationId);
    ensure(() => closedAgentPush.payload.state.status === ConversationStatuses.Closed);

    // 18. A closed conversation rejects further messages with an error response.
    await expectFailure(
      reconnectingCustomer.request(sendChatMessageReq(opened.conversationId, 'after close')).submit<SendChatMessageRes>(signal),
      'Closed conversation must reject follow-up messages.'
    );

    // Agent unavailable: a new conversation stays WaitingForAgent without an error.
    await waitingCustomer.connect(signal);
    const waitingAuth = await waitingCustomer.request(authenticateReq('customer-2')).submit<AuthenticateRes>(signal);
    ensure(() => waitingAuth.actorId === 'customer-2');
    const noAgent = await waitingCustomer.request(openConversationReq('agent unavailable')).submit<OpenConversationRes>(signal);
    ensure(() => noAgent.state.status === ConversationStatuses.WaitingForAgent);
    ensure(() => noAgent.state.subject === 'agent unavailable');
    await expectNoPush<ConversationClosedNotify>(waitingCustomer, PacketNames.conversationClosedNotify, signal);

    // Customer actor SetAgentAvailableReq is rejected with an error response.
    await expectFailure(
      waitingCustomer.request(setAgentAvailableReq(true)).submit<SetAgentAvailableRes>(signal),
      'Customer actor SetAgentAvailableReq must be rejected.'
    );

    // Explicit close scenario with a second agent and customer.
    await closingAgent.connect(signal);
    const closingAgentAuth = await closingAgent.request(authenticateReq('agent-2')).submit<AuthenticateRes>(signal);
    ensure(() => closingAgentAuth.actorId === 'agent-2');
    const secondAvailable = await closingAgent.request(setAgentAvailableReq(true)).submit<SetAgentAvailableRes>(signal);
    ensure(() => secondAvailable.isAvailable);

    await closingCustomer.connect(signal);
    const closingCustomerAuth = await closingCustomer.request(authenticateReq('customer-3')).submit<AuthenticateRes>(signal);
    ensure(() => closingCustomerAuth.actorId === 'customer-3');
    const explicitJoinedForCustomer = closingCustomer.waitFor<ParticipantJoinedNotify>(PacketNames.participantJoinedNotify).submit(signal);
    const explicitAssignedForAgent = closingAgent.waitFor<ConversationAssignedNotify>(PacketNames.conversationAssignedNotify).submit(signal);
    const explicitOpened = await closingCustomer.request(openConversationReq('explicit close')).submit<OpenConversationRes>(signal);
    const explicitJoinedPush = await explicitJoinedForCustomer;
    ensure(() => explicitJoinedPush.payload.actorId === closingAgentAuth.actorId);
    ensure(() => explicitJoinedPush.payload.state.status === ConversationStatuses.Active);
    const explicitAssignedPush = await explicitAssignedForAgent;
    ensure(() => explicitAssignedPush.payload.state.agentActorId === closingAgentAuth.actorId);

    const explicitClosedForAgent = closingAgent.waitFor<ConversationClosedNotify>(PacketNames.conversationClosedNotify).submit(signal);
    const explicitClosed = await closingCustomer.request(closeConversationReq(explicitOpened.conversationId, 'done')).submit<CloseConversationRes>(signal);
    ensure(() => explicitClosed.state.status === ConversationStatuses.Closed);
    const explicitClosedPush = await explicitClosedForAgent;
    ensure(() => explicitClosedPush.payload.conversationId === explicitOpened.conversationId);
    ensure(() => explicitClosedPush.payload.state.status === ConversationStatuses.Closed);

    await expectFailure(
      closingCustomer.request(closeConversationReq(explicitOpened.conversationId, 'again')).submit<CloseConversationRes>(signal),
      'Closed conversation must reject a duplicate close.'
    );
  }
}

async function expectFailure<T>(request: Promise<T>, message: string): Promise<void> {
  let result: T;
  try {
    result = await request;
  } catch (error) {
    if (error instanceof Error && error.name === 'AbortError') {
      throw error;
    }
    return;
  }
  if (typeof result === 'object' && result !== null && 'error' in result) {
    return;
  }
  throw new Error(message);
}

async function expectNoPush<TPayload>(
  client: ZlinkStreamConnector,
  packetName: string,
  signal?: AbortSignal
): Promise<void> {
  const waitTask = client.waitFor<TPayload>(packetName)
    .submit(signal)
    .then(() => true)
    .catch(() => false);
  const received = await Promise.race([
    waitTask,
    noPushWindow(signal).then(() => false)
  ]);
  if (received) {
    throw new Error(`Unexpected stream push '${packetName}'.`);
  }
}

function noPushWindow(signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(resolve, SampleTimings.noPushTimeout);
    signal?.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(new DOMException('Operation aborted.', 'AbortError'));
    }, { once: true });
  });
}

function ensure(condition: () => boolean): void {
  if (!condition()) {
    throw new Error(`Ensure failed: ${conditionExpression(condition)}`);
  }
}

function conditionExpression(condition: () => boolean): string {
  return condition
    .toString()
    .replace(/^\s*\(\)\s*=>\s*/, '')
    .replace(/^\s*function\s*\(\)\s*\{\s*return\s*/, '')
    .replace(/;?\s*\}\s*$/, '')
    .trim();
}

export { SupportChatClientScenario };

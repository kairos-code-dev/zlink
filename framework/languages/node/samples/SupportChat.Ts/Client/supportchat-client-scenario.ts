import {
  ConversationStatuses,
  PacketNames,
  SupportChatRoles,
  authenticate,
  closeConversation,
  joinConversation,
  openConversation,
  sendChatMessage,
  setAgentAvailable,
  setTyping
} from '../Shared/Contracts/messages';
import { SampleNames } from './Configuration/sample-names';
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
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

class SupportChatClientScenario {
  async run(agent: ZlinkStreamConnector, customer1: ZlinkStreamConnector, customer2: ZlinkStreamConnector,
    reconnectedAgent: ZlinkStreamConnector, reconnectedCustomer: ZlinkStreamConnector,
    customer3: ZlinkStreamConnector, customer4: ZlinkStreamConnector,
    customer5: ZlinkStreamConnector, customer6: ZlinkStreamConnector,
    signal?: AbortSignal): Promise<void> {
    await agent.connect(signal);
    const agentAuth = await request<AuthenticateRes>(agent, PacketNames.authenticateReq, authenticate('agent-1'), undefined, signal);
    ensure(() => agentAuth.role === SupportChatRoles.Agent);
    const available = await request<SetAgentAvailableRes>(agent, PacketNames.setAgentAvailableReq, setAgentAvailable(true), undefined, signal);
    ensure(() => available.isAvailable);
    await expectFailure(
      () => request(agent, PacketNames.openConversationReq, openConversation('agent must not open'), undefined, signal),
      'Agent open must fail.'
    );

    await customer1.connect(signal);
    const customer1Auth = await request<AuthenticateRes>(customer1, PacketNames.authenticateReq, authenticate('customer-1'), undefined, signal);
    ensure(() => customer1Auth.role === SupportChatRoles.Customer);
    const assigned1Task = wait<ConversationAssignedNotify>(agent, PacketNames.conversationAssignedNotify, signal);
    const opened1 = await request<OpenConversationRes>(customer1, PacketNames.openConversationReq, openConversation('checkout payment failed'), undefined, signal);
    const cid1 = opened1.conversationId;
    ensure(() => opened1.state.status === ConversationStatuses.WaitingForAgent);
    ensure(() => opened1.state.subject === 'checkout payment failed');
    const assigned1 = await assigned1Task;
    ensure(() => assigned1.payload.conversationId === cid1);

    const joinedCustomer1 = wait<ParticipantJoinedNotify>(customer1, PacketNames.participantJoinedNotify, signal);
    const agentJoin1 = await request<JoinConversationRes>(agent, PacketNames.joinConversationReq, joinConversation(), cid1, signal);
    ensure(() => agentJoin1.state.status === ConversationStatuses.Active);
    const customer1Joined = await joinedCustomer1;
    ensure(() => customer1Joined.payload.actorId === 'agent-1');

    const greeting1Task = wait<ChatMessageNotify>(customer1, PacketNames.chatMessageNotify, signal);
    const greeting1 = await request<SendChatMessageRes>(agent, PacketNames.sendChatMessageReq, sendChatMessage('How can I help?'), cid1, signal);
    ensure(() => greeting1.message.messageSeq === 1);
    const greeting1Push = await greeting1Task;
    ensure(() => greeting1Push.payload.message.messageSeq === 1);
    const reply1Task = wait<ChatMessageNotify>(agent, PacketNames.chatMessageNotify, signal);
    const reply1 = await request<SendChatMessageRes>(customer1, PacketNames.sendChatMessageReq, sendChatMessage('Payment keeps failing.'), cid1, signal);
    ensure(() => reply1.message.messageSeq === 2);
    const reply1Push = await reply1Task;
    ensure(() => reply1Push.payload.message.messageSeq === 2);

    await customer2.connect(signal);
    await request<AuthenticateRes>(customer2, PacketNames.authenticateReq, authenticate('customer-2'), undefined, signal);
    const assigned2Task = wait<ConversationAssignedNotify>(agent, PacketNames.conversationAssignedNotify, signal);
    const opened2 = await request<OpenConversationRes>(customer2, PacketNames.openConversationReq, openConversation('cannot log in'), undefined, signal);
    const cid2 = opened2.conversationId;
    ensure(() => cid2 !== cid1);
    const assigned2 = await assigned2Task;
    ensure(() => assigned2.payload.conversationId === cid2);
    const joinedCustomer2 = wait<ParticipantJoinedNotify>(customer2, PacketNames.participantJoinedNotify, signal);
    const agentJoin2 = await request<JoinConversationRes>(agent, PacketNames.joinConversationReq, joinConversation(), cid2, signal);
    ensure(() => agentJoin2.state.status === ConversationStatuses.Active);
    const customer2Joined = await joinedCustomer2;
    ensure(() => customer2Joined.payload.conversationId === cid2);
    const greeting2Task = wait<ChatMessageNotify>(customer2, PacketNames.chatMessageNotify, signal);
    const greeting2 = await request<SendChatMessageRes>(agent, PacketNames.sendChatMessageReq, sendChatMessage('Let me check your account.'), cid2, signal);
    ensure(() => greeting2.message.messageSeq === 1);
    const greeting2Push = await greeting2Task;
    ensure(() => greeting2Push.payload.message.messageSeq === 1);

    await customer3.connect(signal);
    await request<AuthenticateRes>(customer3, PacketNames.authenticateReq, authenticate('customer-3'), undefined, signal);
    const assigned3Task = wait<ConversationAssignedNotify>(agent, PacketNames.conversationAssignedNotify, signal);
    const opened3 = await request<OpenConversationRes>(customer3, PacketNames.openConversationReq, openConversation('refund delayed'), undefined, signal);
    const cid3 = opened3.conversationId;
    const assigned3 = await assigned3Task;
    ensure(() => assigned3.payload.conversationId === cid3);
    const joinedCustomer3 = wait<ParticipantJoinedNotify>(customer3, PacketNames.participantJoinedNotify, signal);
    const agentJoin3 = await request<JoinConversationRes>(agent, PacketNames.joinConversationReq, joinConversation(), cid3, signal);
    ensure(() => agentJoin3.state.status === ConversationStatuses.Active);
    await joinedCustomer3;
    const greeting3Task = wait<ChatMessageNotify>(customer3, PacketNames.chatMessageNotify, signal);
    const greeting3 = await request<SendChatMessageRes>(agent, PacketNames.sendChatMessageReq, sendChatMessage('I will check the refund.'), cid3, signal);
    ensure(() => greeting3.message.messageSeq === 1);
    await greeting3Task;

    await customer4.connect(signal);
    await expectFailure(() => request(customer4, PacketNames.openConversationReq, openConversation('before auth'), undefined, signal), 'Unauthenticated open must fail.');
    await expectFailure(
      () => request(customer4, PacketNames.sendChatMessageReq, sendChatMessage('before auth'), 'unknown', signal),
      'Unauthenticated message must fail.'
    );
    send(customer4, PacketNames.setTypingReq, setTyping(true), cid1);
    await expectNoPush(customer1, PacketNames.typingChangedNotify, signal);
    await request<AuthenticateRes>(customer4, PacketNames.authenticateReq, authenticate('customer-4'), undefined, signal);
    const atCapacity = await request<OpenConversationRes>(customer4, PacketNames.openConversationReq, openConversation('capacity wait'), undefined, signal);
    ensure(() => atCapacity.state.status === ConversationStatuses.WaitingForAgent);
    await expectNoPush(agent, PacketNames.conversationAssignedNotify, signal);

    await expectFailure(() => request(customer2, PacketNames.sendChatMessageReq, sendChatMessage('not my room'), cid1, signal), 'Nonparticipant message must fail.');
    send(customer2, PacketNames.setTypingReq, setTyping(true), cid1);
    await expectNoPush(customer1, PacketNames.typingChangedNotify, signal);
    const typingTask = wait<TypingChangedNotify>(customer1, PacketNames.typingChangedNotify, signal);
    send(agent, PacketNames.setTypingReq, setTyping(true), cid1);
    const typing = await typingTask;
    ensure(() => typing.payload.actorId === 'agent-1');

    await customer1.close(signal);
    await reconnectedCustomer.connect(signal);
    const reconnectedCustomerAuth = await request<AuthenticateRes>(
      reconnectedCustomer,
      PacketNames.authenticateReq,
      authenticate('customer-1'),
      undefined,
      signal
    );
    ensure(() => reconnectedCustomerAuth.actorId === 'customer-1');
    const customerRejoined1 = await request<JoinConversationRes>(
      reconnectedCustomer,
      PacketNames.joinConversationReq,
      joinConversation(),
      cid1,
      signal
    );
    ensure(() => customerRejoined1.state.subject === 'checkout payment failed' && customerRejoined1.state.lastMessageSeq === 2);

    await agent.close(signal);
    await reconnectedAgent.connect(signal);
    await request<AuthenticateRes>(reconnectedAgent, PacketNames.authenticateReq, authenticate('agent-1'), undefined, signal);
    const reconnectedAvailable = await request<SetAgentAvailableRes>(reconnectedAgent, PacketNames.setAgentAvailableReq, setAgentAvailable(true), undefined, signal);
    ensure(() => reconnectedAvailable.isAvailable);
    const rejoined1 = await request<JoinConversationRes>(reconnectedAgent, PacketNames.joinConversationReq, joinConversation(), cid1, signal);
    const rejoined2 = await request<JoinConversationRes>(reconnectedAgent, PacketNames.joinConversationReq, joinConversation(), cid2, signal);
    const rejoined3 = await request<JoinConversationRes>(reconnectedAgent, PacketNames.joinConversationReq, joinConversation(), cid3, signal);
    ensure(() => rejoined1.state.subject === 'checkout payment failed' && rejoined1.state.lastMessageSeq === 2);
    ensure(() => rejoined2.state.subject === 'cannot log in' && rejoined2.state.lastMessageSeq === 1);
    ensure(() => rejoined3.state.subject === 'refund delayed' && rejoined3.state.lastMessageSeq === 1);

    const closed2Notify = waitConversation<ConversationClosedNotify>(reconnectedAgent, PacketNames.conversationClosedNotify, cid2, signal);
    const closed2 = await request<CloseConversationRes>(customer2, PacketNames.closeConversationReq, closeConversation('resolved'), cid2, signal);
    ensure(() => closed2.state.status === ConversationStatuses.Closed);
    const closed2Push = await closed2Notify;
    ensure(() => closed2Push.payload.state.status === ConversationStatuses.Closed);
    await expectFailure(() => request(customer2, PacketNames.closeConversationReq, closeConversation(), cid2, signal), 'Duplicate close must fail.');

    const firstIdleCustomer = wait<ConversationIdleNotify>(reconnectedCustomer, PacketNames.conversationIdleNotify, signal);
    const firstIdleAgent = waitConversation<ConversationIdleNotify>(reconnectedAgent, PacketNames.conversationIdleNotify, cid1, signal);
    const [firstCustomerIdle, firstAgentIdle] = await Promise.all([firstIdleCustomer, firstIdleAgent]);
    ensure(() => firstCustomerIdle.payload.state.status === ConversationStatuses.WaitingForClose);
    ensure(() => firstAgentIdle.payload.state.status === ConversationStatuses.WaitingForClose);
    const resumedPush = waitConversation<ChatMessageNotify>(reconnectedAgent, PacketNames.chatMessageNotify, cid1, signal);
    const resumed = await request<SendChatMessageRes>(reconnectedCustomer, PacketNames.sendChatMessageReq, sendChatMessage('I am still here.'), cid1, signal);
    ensure(() => resumed.state.status === ConversationStatuses.Active);
    const resumedNotification = await resumedPush;
    ensure(() => resumedNotification.payload.message.messageSeq === 3);

    const secondIdleCustomer = wait<ConversationIdleNotify>(reconnectedCustomer, PacketNames.conversationIdleNotify, signal);
    const secondIdleAgent = waitConversation<ConversationIdleNotify>(reconnectedAgent, PacketNames.conversationIdleNotify, cid1, signal);
    const idleClosedCustomer = wait<ConversationClosedNotify>(reconnectedCustomer, PacketNames.conversationClosedNotify, signal);
    const idleClosedAgent = waitConversation<ConversationClosedNotify>(reconnectedAgent, PacketNames.conversationClosedNotify, cid1, signal);
    await Promise.all([secondIdleCustomer, secondIdleAgent]);
    const [customerIdleClosed, agentIdleClosed] = await Promise.all([idleClosedCustomer, idleClosedAgent]);
    ensure(() => customerIdleClosed.payload.state.status === ConversationStatuses.Closed);
    ensure(() => agentIdleClosed.payload.state.status === ConversationStatuses.Closed);
    await expectFailure(() => request(reconnectedCustomer, PacketNames.sendChatMessageReq, sendChatMessage('too late'), cid1, signal), 'Closed conversation message must fail.');
    send(reconnectedCustomer, PacketNames.setTypingReq, setTyping(true), cid1);
    await expectNoPush(reconnectedAgent, PacketNames.typingChangedNotify, signal);
    console.log('supportchat-closed-typing-ignore=verified');

    await customer5.connect(signal);
    await request<AuthenticateRes>(customer5, PacketNames.authenticateReq, authenticate('customer-5'), undefined, signal);
    await expectFailure(() => request(customer5, PacketNames.setAgentAvailableReq, setAgentAvailable(true), undefined, signal), 'Customer availability must fail.');
    const recoveredAssignmentTask = wait<ConversationAssignedNotify>(reconnectedAgent, PacketNames.conversationAssignedNotify, signal);
    const capacityRecovered = await request<OpenConversationRes>(customer5, PacketNames.openConversationReq, openConversation('capacity recovered'), undefined, signal);
    const recoveredAssignment = await recoveredAssignmentTask;
    ensure(() => recoveredAssignment.payload.conversationId === capacityRecovered.conversationId);

    const unavailable = await request<SetAgentAvailableRes>(reconnectedAgent, PacketNames.setAgentAvailableReq, setAgentAvailable(false), undefined, signal);
    ensure(() => !unavailable.isAvailable);
    await customer6.connect(signal);
    await request<AuthenticateRes>(customer6, PacketNames.authenticateReq, authenticate('customer-6'), undefined, signal);
    const noAgent = await request<OpenConversationRes>(customer6, PacketNames.openConversationReq, openConversation('agent unavailable'), undefined, signal);
    ensure(() => noAgent.state.status === ConversationStatuses.WaitingForAgent);
    ensure(() => noAgent.state.subject === 'agent unavailable');
    await expectNoPush(customer6, PacketNames.conversationClosedNotify, signal);
  }
}

function request<T>(client: ZlinkStreamConnector, packetName: string, payload: unknown, conversationId?: string, signal?: AbortSignal): Promise<T> {
  const call = client.request(payload, Object).packetName(packetName);
  if (conversationId !== undefined) call.metadata(SampleNames.conversationIdMetadataKey, conversationId);
  return call.submit<T>(signal);
}
function send(client: ZlinkStreamConnector, packetName: string, payload: unknown, conversationId: string): void {
  client.send(payload, Object).packetName(packetName).metadata(SampleNames.conversationIdMetadataKey, conversationId).submit();
}
function wait<T>(client: ZlinkStreamConnector, packetName: string, signal?: AbortSignal) {
  return client.waitFor<T>(packetName).submit(signal);
}
function waitConversation<T>(client: ZlinkStreamConnector, packetName: string, conversationId: string, signal?: AbortSignal) {
  return client
    .waitFor<T>(packetName)
    .where((message) => message.metadata.get(SampleNames.conversationIdMetadataKey) === conversationId)
    .submit(signal);
}
async function expectFailure(action: () => Promise<unknown>, message: string): Promise<void> {
  try { await action(); } catch { return; }
  throw new Error(message);
}
async function expectNoPush(client: ZlinkStreamConnector, packetName: string, signal?: AbortSignal): Promise<void> {
  try { await client.waitFor(packetName).timeout(250).submit(signal); } catch { return; }
  throw new Error(`Unexpected '${packetName}' push.`);
}
function ensure(condition: boolean | (() => boolean)): void {
  const ok = typeof condition === 'function' ? condition() : condition;
  if (!ok) throw new Error('Ensure failed.');
}

export { SupportChatClientScenario };

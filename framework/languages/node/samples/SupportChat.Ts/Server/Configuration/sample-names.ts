import { PacketNames, SupportSampleUsers } from '../../Shared/Contracts/messages';
const SampleNames = {
  apiChannel: 'supportchat.api',
  supportChannel: 'supportchat.support',
  notificationChannel: 'supportchat.notifications',
  sessionStream: 'supportchat.session.stream',
  supportActorType: 'supportchat.user',
  conversationSpotType: 'supportchat.conversation',
  customerActorIds: [SupportSampleUsers.customer1, SupportSampleUsers.customer2, SupportSampleUsers.customer3],
  agentActorIds: [SupportSampleUsers.agent1, SupportSampleUsers.agent2],
  participantJoinedPacket: PacketNames.participantJoinedNotify,
  conversationAssignedPacket: PacketNames.conversationAssignedNotify,
  chatMessagePacket: PacketNames.chatMessageNotify,
  typingChangedPacket: PacketNames.typingChangedNotify,
  conversationIdlePacket: PacketNames.conversationIdleNotify,
  conversationClosedPacket: PacketNames.conversationClosedNotify
};

const SampleTimings = {
  requestTimeout: 10000,
  idleTimeoutMs: 3000,
  closeGraceTimeoutMs: 2000,
  idleCheckPeriodMs: 200,
  maxMessageLength: 500
};

export { SampleNames, SampleTimings };

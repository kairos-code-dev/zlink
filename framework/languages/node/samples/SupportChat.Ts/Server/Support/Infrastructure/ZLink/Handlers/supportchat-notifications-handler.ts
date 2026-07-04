import { PacketNames } from '../../../../../Shared/Contracts/messages';

class SupportChatNotificationsHandler {
  names(): string[] {
    return [
      PacketNames.conversationAssignedNotify,
      PacketNames.participantJoinedNotify,
      PacketNames.chatMessageNotify,
      PacketNames.typingChangedNotify,
      PacketNames.conversationIdleNotify,
      PacketNames.conversationClosedNotify
    ];
  }
}

export { SupportChatNotificationsHandler };

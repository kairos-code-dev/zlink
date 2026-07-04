import { PacketNames } from '../../../../../../../Shared/Contracts/messages';

class OpenConversationActorHandler {
  packetName(): string {
    return PacketNames.openConversationReq;
  }
}

export { OpenConversationActorHandler };

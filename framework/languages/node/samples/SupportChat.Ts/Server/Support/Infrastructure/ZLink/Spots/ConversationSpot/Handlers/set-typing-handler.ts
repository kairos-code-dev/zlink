import { PacketNames } from '../../../../../../../Shared/Contracts/messages';

class SetTypingHandler {
  packetName(): string {
    return PacketNames.setTypingReq;
  }
}

export { SetTypingHandler };

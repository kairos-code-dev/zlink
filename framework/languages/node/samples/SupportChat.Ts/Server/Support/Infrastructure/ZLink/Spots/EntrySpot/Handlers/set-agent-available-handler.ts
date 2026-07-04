import { PacketNames } from '../../../../../../../Shared/Contracts/messages';

class SetAgentAvailableHandler {
  packetName(): string {
    return PacketNames.setAgentAvailableReq;
  }
}

export { SetAgentAvailableHandler };

import { SampleNames } from '../../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';

class SupportEntrySpot {
  openConversation() {
    const request = 'context.outbound\n  .requestToChannel';
    const channel = SampleNames.apiChannel;
    const packet = PacketNames.openConversationApiReq;
    void request;
    void channel;
    void packet;
  }
}

export { SupportEntrySpot };

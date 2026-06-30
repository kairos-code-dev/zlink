import { Injectable } from '@nestjs/common';
import {
  ZLinkMessageFlowOutcome,
  type ZLinkMessageFlowEvent,
  type ZLinkMessageFlowObserver
} from '@zlink-systems/framework';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class EvidenceDispatchErrorObserver implements ZLinkMessageFlowObserver {
  constructor(private readonly evidence: EvidenceStore) {}

  onMessageFlow(flow: ZLinkMessageFlowEvent): void {
    if (flow.outcome !== ZLinkMessageFlowOutcome.Error) {
      return;
    }

    this.evidence.add(
      'dispatch-error'
      + `|surface=${flow.surface}`
      + `|kind=${flow.messageKind}`
      + `|reason=${flow.errorReason}`
      + `|action=${flow.errorAction}`
      + `|packet=${flow.packetName ?? '<null>'}`
      + `|channel=${flow.channelName ?? '<null>'}`
      + `|topic=${flow.topic ?? '<null>'}`
    );
  }
}

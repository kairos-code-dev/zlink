import { Injectable } from '@nestjs/common';
import {
  type ZLinkMessageFlowEvent,
  type ZLinkMessageFlowObserver,
  ZLinkMessageFlowOutcome
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
      + `|errorType=${flow.errorType ?? '<null>'}`
      + `|errorMessage=${flow.errorMessage ?? '<null>'}`
    );
  }
}

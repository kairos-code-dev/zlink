import { Inject, Injectable } from '@nestjs/common';
import {
  ZLinkMessageFlowOutcome,
  type ZLinkMessageFlowEvent,
  type ZLinkMessageFlowObserver,
  type ZLinkPublishContext,
  type ZLinkPublishHandler
} from '@zlink-systems/framework';
import { PubSubNames, type EventNotify } from '../../../Shared/messages';
import { SUBSCRIBER_OPTIONS, type SubscriberOptions } from '../Configuration/subscriber-options';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class EventNotifyHandler implements ZLinkPublishHandler<EventNotify> {
  constructor(
    private readonly evidence: EvidenceStore,
    @Inject(SUBSCRIBER_OPTIONS)
    private readonly options: SubscriberOptions
  ) {}

  async handle(message: EventNotify, context: ZLinkPublishContext): Promise<void> {
    if (this.options.handlerDelayMs > 0 && message.value.startsWith('slow-')) {
      this.evidence.add(
        `delay-start|rid=${this.evidence.rid}|run=${message.runId}|topic=${context.topic}`
        + `|seq=${message.sequence}|value=${message.value}`
      );
      await new Promise((resolve) => setTimeout(resolve, this.options.handlerDelayMs));
    }

    if (context.topic === PubSubNames.mainTopic) {
      this.evidence.add(
        `event|rid=${this.evidence.rid}|run=${message.runId}|topic=${context.topic}`
        + `|seq=${message.sequence}|value=${message.value}|packet=${context.packetName}`
      );
    } else {
      this.evidence.add(
        `ignored|rid=${this.evidence.rid}|run=${message.runId}|topic=${context.topic}`
        + `|seq=${message.sequence}|value=${message.value}|packet=${context.packetName}`
      );
    }
  }
}

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

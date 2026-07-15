import { Injectable } from '@nestjs/common';
import type {
  ZLinkRuntimeEventHandler,
  ZLinkSocketEvent
} from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import { RuntimeMonitoringNames, socketEventName } from '../../../Shared/messages';
import { EvidenceStore } from '../../Service/Infrastructure/evidence-store';

@Injectable()
@zlinkRuntimeEventHandler()
export class TriggerSocketEventRecorder implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(event: ZLinkSocketEvent): Promise<void> {
    if (event.sourceName !== RuntimeMonitoringNames.channelClientSource) {
      return;
    }
    this.evidence.add(
      `monitor-socket|source=${event.sourceName}|kind=${socketEventName(event.event)}`
      + `|remote=${event.remoteAddr}|routing=${event.routingId ?? '<null>'}`
    );
  }
}

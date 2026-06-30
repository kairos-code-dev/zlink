import { Injectable } from '@nestjs/common';
import type {
  ZLinkRegistryEvent,
  ZLinkRuntimeEventHandler
} from '@zlink-systems/framework';
import { ZLinkRegistryEventKind } from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
@zlinkRuntimeEventHandler()
export class RegistryEventRecorder implements ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(event: ZLinkRegistryEvent): Promise<void> {
    this.evidence.add(
      `monitor-registry|source=${event.sourceName}|kind=${ZLinkRegistryEventKind[event.event]}`
      + `|topology=${event.topology?.length ?? -1}|summary=${event.serviceSummary?.length ?? -1}`
    );
  }
}

import { Injectable } from '@nestjs/common';
import type { ZLinkPublishContext, ZLinkPublishHandler } from '@zlink-systems/framework';
import type { LoadEvent } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class LoadEventHandler implements ZLinkPublishHandler<LoadEvent> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(event: LoadEvent, context: ZLinkPublishContext): Promise<void> {
    this.evidence.add(
      `load-event|rid=${this.evidence.rid}|run=${event.runId}|seq=${event.sequence}`
      + `|topic=${context.topic}|packet=${context.packetName}`
    );
  }
}

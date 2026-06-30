import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { DelayReply, DelayReq } from '../../../Shared/messages';
import { EvidenceStore } from '../../Support/evidence-store';

@Injectable()
export class DelayHandler implements ZLinkRequestHandler<DelayReq, DelayReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: DelayReq, context: ZLinkHandlerContext): Promise<DelayReply> {
    void context;
    this.evidence.add(`delay-started|rid=${this.evidence.rid}|request=${request.requestId}|marker=${request.marker}`);
    await new Promise((resolve) => setTimeout(resolve, request.delayMs));
    this.evidence.add(`delay-completed|rid=${this.evidence.rid}|request=${request.requestId}|marker=${request.marker}`);
    return {
      requestId: request.requestId,
      marker: request.marker,
      nodeRid: this.evidence.rid
    };
  }
}

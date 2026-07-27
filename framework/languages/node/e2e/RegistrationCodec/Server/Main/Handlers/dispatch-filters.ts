import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerDelegate, ZLinkHandlerFilter, ZLinkMessageContext } from '@zlink-systems/framework';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class FirstFilter implements ZLinkHandlerFilter {
  constructor(private readonly evidence: EvidenceStore) {}

  async invoke(context: ZLinkMessageContext, next: ZLinkHandlerDelegate): Promise<unknown> {
    this.evidence.add(`filter|name=first|phase=before|packet=${context.packetName ?? '<null>'}`);
    const result = await next();
    this.evidence.add(`filter|name=first|phase=after|packet=${context.packetName ?? '<null>'}`);
    return result;
  }
}

@Injectable()
export class SecondFilter implements ZLinkHandlerFilter {
  constructor(private readonly evidence: EvidenceStore) {}

  async invoke(context: ZLinkMessageContext, next: ZLinkHandlerDelegate): Promise<unknown> {
    this.evidence.add(`filter|name=second|phase=before|packet=${context.packetName ?? '<null>'}`);
    const result = await next();
    this.evidence.add(`filter|name=second|phase=after|packet=${context.packetName ?? '<null>'}`);
    return result;
  }
}

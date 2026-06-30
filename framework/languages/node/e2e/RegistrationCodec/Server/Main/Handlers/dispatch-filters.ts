import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerDelegate, ZLinkHandlerFilter, ZLinkHandlerInvocation } from '@zlink-systems/framework';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class FirstFilter implements ZLinkHandlerFilter {
  constructor(private readonly evidence: EvidenceStore) {}

  async invoke(invocation: ZLinkHandlerInvocation, next: ZLinkHandlerDelegate): Promise<unknown> {
    this.evidence.add(`filter|name=first|phase=before|packet=${invocation.packetName ?? '<null>'}`);
    const result = await next();
    this.evidence.add(`filter|name=first|phase=after|packet=${invocation.packetName ?? '<null>'}`);
    return result;
  }
}

@Injectable()
export class SecondFilter implements ZLinkHandlerFilter {
  constructor(private readonly evidence: EvidenceStore) {}

  async invoke(invocation: ZLinkHandlerInvocation, next: ZLinkHandlerDelegate): Promise<unknown> {
    this.evidence.add(`filter|name=second|phase=before|packet=${invocation.packetName ?? '<null>'}`);
    const result = await next();
    this.evidence.add(`filter|name=second|phase=after|packet=${invocation.packetName ?? '<null>'}`);
    return result;
  }
}

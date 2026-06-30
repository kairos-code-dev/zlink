import { Injectable, Scope } from '@nestjs/common';
import type { ZLinkRequestContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import { type EchoReply, type EchoReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { ScopedProbe, SingletonProbe } from '../Infrastructure/lifecycle-probes';

@Injectable({ scope: Scope.REQUEST })
export class DiEchoRequestHandler implements ZLinkRequestHandler<EchoReq, EchoReply> {
  constructor(
    private readonly evidence: EvidenceStore,
    private readonly singleton: SingletonProbe,
    private readonly scoped: ScopedProbe
  ) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<EchoReply> {
    this.evidence.add(
      `di|value=${request.value}|singleton=${this.singleton.id}|scoped=${this.scoped.id}|disposed=${ScopedProbe.disposedCount}`
    );
    return { value: `echo:${request.value}`, contentType: context.contentType ?? '<null>' };
  }
}

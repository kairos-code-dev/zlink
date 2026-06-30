import { Injectable } from '@nestjs/common';
import type { ZLinkRequestContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { ProfileRes, ProfileReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ProfileRequestHandler implements ZLinkRequestHandler<ProfileReq, ProfileRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ProfileReq, context: ZLinkRequestContext): Promise<ProfileRes> {
    const marker = request.marker ?? '';
    this.evidence.add(
      `profile-request|rid=${this.evidence.rid}|value=${request.value}|marker=${marker}|packet=${context.packetName}`
    );
    return { value: `profile:${request.value}`, providerRid: this.evidence.rid, marker: request.marker };
  }
}

import { Injectable } from '@nestjs/common';
import type { ZLinkRequestContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { ProfileReply, ProfileRequest } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ProfileRequestHandler implements ZLinkRequestHandler<ProfileRequest, ProfileReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ProfileRequest, context: ZLinkRequestContext): Promise<ProfileReply> {
    const marker = request.marker ?? '';
    this.evidence.add(
      `profile-request|rid=${this.evidence.rid}|value=${request.value}|marker=${marker}|packet=${context.packetName}`
    );
    return { value: `profile:${request.value}`, providerRid: this.evidence.rid, marker: request.marker };
  }
}

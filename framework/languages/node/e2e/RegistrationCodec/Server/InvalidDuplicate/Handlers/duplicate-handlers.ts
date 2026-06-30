import { Injectable } from '@nestjs/common';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import { type EchoReply, type EchoReq } from '../../../Shared/messages';

@Injectable()
export class DuplicateEchoRequestHandler implements ZLinkRequestHandler<EchoReq, EchoReply> {
  async handle(request: EchoReq): Promise<EchoReply> {
    return { value: request.value, contentType: 'duplicate' };
  }
}

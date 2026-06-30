import { Injectable } from '@nestjs/common';
import type { ZLinkRequestContext, ZLinkRequestHandler, ZLinkSendContext, ZLinkSendHandler } from '@zlink-systems/framework';
import type { ChannelEchoReply, ChannelEchoReq, ChannelNotify } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ChannelEchoHandler implements ZLinkRequestHandler<ChannelEchoReq, ChannelEchoReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ChannelEchoReq, context: ZLinkRequestContext): Promise<ChannelEchoReply> {
    void context;
    this.evidence.add(`channel-echo|value=${request.value}`);
    return { value: `echo-${request.value}` };
  }
}

@Injectable()
export class ChannelNotifyHandler implements ZLinkSendHandler<ChannelNotify> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: ChannelNotify, context: ZLinkSendContext): Promise<void> {
    void context;
    this.evidence.add(`channel-notify|marker=${message.marker}`);
  }
}

import { Injectable } from '@nestjs/common';
import type {
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkSendContext,
  ZLinkSendHandler
} from '@zlink-systems/framework';
import { type CodecScenarioRes, type EchoMsg, type EchoReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ProtobufEchoRequestHandler implements ZLinkRequestHandler<EchoReq, CodecScenarioRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<CodecScenarioRes> {
    this.evidence.add(`codec-request|codec=protobuf|value=${request.value}|content=${context.contentType}`);
    return { value: `echo:${request.value}`, contentType: context.contentType ?? '<null>' };
  }
}

@Injectable()
export class ProtobufEchoCommandHandler implements ZLinkSendHandler<EchoMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: EchoMsg, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`codec-command|codec=protobuf|id=${message.commandId}|value=${message.value}|content=${context.contentType}`);
  }
}

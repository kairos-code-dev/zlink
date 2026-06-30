import { Injectable } from '@nestjs/common';
import type {
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkSendContext,
  ZLinkSendHandler
} from '@zlink-systems/framework';
import { type CodecScenarioResult, type EchoCommand, type EchoReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ProtobufEchoRequestHandler implements ZLinkRequestHandler<EchoReq, CodecScenarioResult> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<CodecScenarioResult> {
    this.evidence.add(`codec-request|codec=protobuf|value=${request.value}|content=${context.contentType}`);
    return { value: `echo:${request.value}`, contentType: context.contentType ?? '<null>' };
  }
}

@Injectable()
export class ProtobufEchoCommandHandler implements ZLinkSendHandler<EchoCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: EchoCommand, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`codec-command|codec=protobuf|id=${message.commandId}|value=${message.value}|content=${context.contentType}`);
  }
}

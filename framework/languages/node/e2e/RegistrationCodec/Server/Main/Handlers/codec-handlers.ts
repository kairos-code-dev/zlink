import { Injectable } from '@nestjs/common';
import type {
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkSendContext,
  ZLinkSendHandler
} from '@zlink-systems/framework';
import {
  MessagePackEchoReq,
  ProtobufEchoReq,
  type EchoCommand,
  type EchoReply,
  type EchoReq,
  type MessagePackEchoCommand,
  type ProtobufEchoCommand
} from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class JsonEchoRequestHandler implements ZLinkRequestHandler<EchoReq, EchoReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<EchoReply> {
    this.evidence.add(`codec-request|codec=json|value=${request.value}|content=${context.contentType}`);
    return { value: `echo:${request.value}`, contentType: context.contentType ?? '<null>' };
  }
}

@Injectable()
export class JsonEchoCommandHandler implements ZLinkSendHandler<EchoCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: EchoCommand, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`codec-command|codec=json|id=${message.commandId}|value=${message.value}|content=${context.contentType}`);
  }
}

@Injectable()
export class ProtobufEchoRequestHandler implements ZLinkRequestHandler<EchoReq, ProtobufEchoReq> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<ProtobufEchoReq> {
    this.evidence.add(`codec-request|codec=protobuf|value=${request.value}|content=${context.contentType}`);
    return new ProtobufEchoReq(`echo:${request.value}|content:${context.contentType ?? '<null>'}`);
  }
}

@Injectable()
export class ProtobufEchoCommandHandler implements ZLinkSendHandler<ProtobufEchoCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: ProtobufEchoCommand, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`codec-command|codec=protobuf|value=${message.value}|content=${context.contentType}`);
  }
}

@Injectable()
export class MessagePackEchoRequestHandler implements ZLinkRequestHandler<EchoReq, MessagePackEchoReq> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<MessagePackEchoReq> {
    this.evidence.add(`codec-request|codec=msgpack|value=${request.value}|content=${context.contentType}`);
    return new MessagePackEchoReq(`echo:${request.value}|content:${context.contentType ?? '<null>'}`);
  }
}

@Injectable()
export class MessagePackEchoCommandHandler implements ZLinkSendHandler<MessagePackEchoCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: MessagePackEchoCommand, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`codec-command|codec=msgpack|id=${message.commandId}|value=${message.value}|content=${context.contentType}`);
  }
}

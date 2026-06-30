import { Injectable } from '@nestjs/common';
import {
  zlinkRequestHandler,
  zlinkSendHandler
} from '@zlink-systems/nestjs';
import type {
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkSendContext,
  ZLinkSendHandler
} from '@zlink-systems/framework';
import { PacketNames, type EchoCommand, type EchoReply, type EchoReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@zlinkRequestHandler('auto', PacketNames.echoAuto)
@Injectable()
export class EchoAutoRequestHandler implements ZLinkRequestHandler<EchoReq, EchoReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<EchoReply> {
    this.evidence.add(`echo-request|variant=auto|value=${request.value}|content=${context.contentType}`);
    return { value: `echo:${request.value}`, contentType: context.contentType ?? '<null>' };
  }
}

@zlinkSendHandler('auto', PacketNames.echoAutoCommand)
@Injectable()
export class EchoAutoCommandHandler implements ZLinkSendHandler<EchoCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: EchoCommand, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`echo-command|variant=auto|id=${message.commandId}|value=${message.value}|content=${context.contentType}`);
  }
}

@zlinkRequestHandler('attr', PacketNames.echoAttr)
@Injectable()
export class EchoAttrRequestHandler implements ZLinkRequestHandler<EchoReq, EchoReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<EchoReply> {
    this.evidence.add(`echo-request|variant=attr|value=${request.value}|content=${context.contentType}`);
    return { value: `echo:${request.value}`, contentType: context.contentType ?? '<null>' };
  }
}

@zlinkSendHandler('attr', PacketNames.echoAttrCommand)
@Injectable()
export class EchoAttrCommandHandler implements ZLinkSendHandler<EchoCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: EchoCommand, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`echo-command|variant=attr|id=${message.commandId}|value=${message.value}|content=${context.contentType}`);
  }
}

@Injectable()
export class EchoManualRequestHandler implements ZLinkRequestHandler<EchoReq, EchoReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: EchoReq, context: ZLinkRequestContext): Promise<EchoReply> {
    this.evidence.add(`echo-request|variant=manual|value=${request.value}|content=${context.contentType}`);
    return { value: `echo:${request.value}`, contentType: context.contentType ?? '<null>' };
  }
}

@Injectable()
export class EchoManualCommandHandler implements ZLinkSendHandler<EchoCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(message: EchoCommand, context: ZLinkSendContext): Promise<void> {
    this.evidence.add(`echo-command|variant=manual|id=${message.commandId}|value=${message.value}|content=${context.contentType}`);
  }
}

@Injectable()
export class DuplicateEchoRequestHandler implements ZLinkRequestHandler<EchoReq, EchoReply> {
  async handle(request: EchoReq): Promise<EchoReply> {
    return { value: request.value, contentType: 'duplicate' };
  }
}

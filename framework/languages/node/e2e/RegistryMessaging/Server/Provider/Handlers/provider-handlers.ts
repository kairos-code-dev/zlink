import { Injectable } from '@nestjs/common';
import type {
  ZLinkMessageFlowEvent,
  ZLinkMessageFlowObserver,
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkRouteRequestContext,
  ZLinkRouteRequestHandler,
  ZLinkSendContext,
  ZLinkSendHandler
} from '@zlink-systems/framework';
import { ZLinkMessageFlowOutcome } from '@zlink-systems/framework';
import type {
  PayloadReply,
  PayloadRequest,
  ProfileCommand,
  ProfileReply,
  ProfileRequest,
  ScenarioRoutePing,
  ScenarioRoutePong
} from '../../../Shared/messages';
import { sha256Hex } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ProfileRequestHandler implements ZLinkRequestHandler<ProfileRequest, ProfileReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ProfileRequest, context: ZLinkRequestContext): Promise<ProfileReply> {
    if (request.value === 'slow') {
      await delay(1000);
    }
    this.evidence.add(`profile-request|rid=${this.evidence.rid}|value=${request.value}|packet=${context.packetName}`);
    return { value: `profile:${request.value}`, providerRid: this.evidence.rid };
  }
}

@Injectable()
export class ProfileCommandHandler implements ZLinkSendHandler<ProfileCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(command: ProfileCommand, context: ZLinkSendContext): Promise<void> {
    if (command.commandId.startsWith('rm-c9-slow-')) {
      await delay(1000);
    }
    this.evidence.add(`profile-command|rid=${this.evidence.rid}|command=${command.commandId}|packet=${context.packetName}`);
  }
}

@Injectable()
export class PayloadRequestHandler implements ZLinkRequestHandler<PayloadRequest, PayloadReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: PayloadRequest, context: ZLinkRequestContext): Promise<PayloadReply> {
    const hash = sha256Hex(request.payload);
    this.evidence.add(
      `payload-request|rid=${this.evidence.rid}|marker=${request.marker}`
      + `|length=${request.payload.length}|sha256=${hash}|packet=${context.packetName}`
    );
    return { marker: request.marker, length: request.payload.length, sha256: hash };
  }
}

@Injectable()
export class RoutePingHandler implements ZLinkRouteRequestHandler<ScenarioRoutePing, ScenarioRoutePong> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ScenarioRoutePing, context: ZLinkRouteRequestContext): Promise<ScenarioRoutePong> {
    const sourceRid = String(context.sourceNodeRid);
    this.evidence.add(`route-request|rid=${this.evidence.rid}|source=${sourceRid}|value=${request.value}`);
    return { value: `route:${request.value}`, providerRid: this.evidence.rid, sourceRid };
  }
}

@Injectable()
export class EvidenceDispatchErrorObserver implements ZLinkMessageFlowObserver {
  constructor(private readonly evidence: EvidenceStore) {}

  onMessageFlow(flow: ZLinkMessageFlowEvent): void {
    if (flow.outcome !== ZLinkMessageFlowOutcome.Error) {
      return;
    }
    this.evidence.add(
      'dispatch-error'
      + `|surface=${flow.surface}`
      + `|kind=${flow.messageKind}`
      + `|reason=${flow.errorReason}`
      + `|action=${flow.errorAction}`
      + `|packet=${flow.packetName ?? '<null>'}`
      + `|channel=${flow.channelName ?? '<null>'}`
    );
  }
}

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

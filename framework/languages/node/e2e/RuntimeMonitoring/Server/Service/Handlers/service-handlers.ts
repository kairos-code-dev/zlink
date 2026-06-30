import { Injectable } from '@nestjs/common';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkRuntimeEventHandler,
  ZLinkSocketEvent,
  ZLinkSpotEvent,
  ZLinkSpotTimerHandler,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import { ZLinkSocketEventKind, ZLinkSpotEventKind } from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler, zlinkSpotTimerHandler } from '@zlink-systems/nestjs';
import { RuntimeMonitoringNames, type ProfileReply, type ProfileRequest } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ProfileRequestHandler implements ZLinkRequestHandler<ProfileRequest, ProfileReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ProfileRequest, context: ZLinkRequestContext): Promise<ProfileReply> {
    this.evidence.add(`profile-request|rid=${this.evidence.rid}|marker=${request.marker}|value=${request.value}|packet=${context.packetName}`);
    return { value: `profile:${request.value}`, providerRid: this.evidence.rid, marker: request.marker };
  }
}

@Injectable()
@zlinkRuntimeEventHandler()
export class SocketEventRecorder implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(event: ZLinkSocketEvent): Promise<void> {
    if (event.sourceName !== RuntimeMonitoringNames.channelServerSource) {
      return;
    }
    this.evidence.add(
      `monitor-socket|source=${event.sourceName}|kind=${ZLinkSocketEventKind[event.event]}`
      + `|remote=${event.remoteAddr}|routing=${event.routingId ?? '<null>'}`
      + `|native=${event.diagnostic?.nativeEvent ?? '<none>'}|value=${event.diagnostic?.nativeValue ?? '<none>'}`
    );
  }
}

@Injectable()
export class MonitoringEntrySpot implements ZLinkEntrySpot {
  readonly context?: ZLinkEntrySpotContext;

  async onInitialize(): Promise<void> {
    if (this.context === undefined) {
      throw new Error('Monitoring entry spot context was not assigned.');
    }
    await this.context.addTimer('failing', 50, FailingTimerHandler, { stopOnUnhandledException: false });
    await this.context.addTimer('stopping', 50, FailingTimerHandler, { stopOnUnhandledException: true });
  }
}

@Injectable()
@zlinkSpotTimerHandler()
export class FailingTimerHandler implements ZLinkSpotTimerHandler<MonitoringEntrySpot> {
  async handle(_spot: MonitoringEntrySpot, _tick: ZLinkTimerTick): Promise<void> {
    throw new Error('monitoring timer failure');
  }
}

@Injectable()
@zlinkRuntimeEventHandler()
export class SpotEventRecorder implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(event: ZLinkSpotEvent): Promise<void> {
    if (event.sourceName !== RuntimeMonitoringNames.spotNode) {
      return;
    }
    this.evidence.add(
      `monitor-spot|source=${event.sourceName}|kind=${ZLinkSpotEventKind[event.event]}`
      + `|peers=${event.peers?.length ?? 0}|subjects=${event.subjects?.length ?? 0}`
      + `|timer=${event.timerDiagnostic?.timerName ?? '<none>'}`
    );
  }
}

@Injectable()
@zlinkRuntimeEventHandler()
export class ThrowingSocketEventRecorder implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(event: ZLinkSocketEvent): Promise<void> {
    if (event.sourceName !== RuntimeMonitoringNames.channelServerSource) {
      return;
    }
    this.evidence.add(`monitor-throw|source=${event.sourceName}|kind=${ZLinkSocketEventKind[event.event]}`);
    throw new Error('monitoring dispatch failure for e2e');
  }
}

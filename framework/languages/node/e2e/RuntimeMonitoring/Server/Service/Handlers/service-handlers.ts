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
  ZLinkLocationRuntimeEvent,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import { ZLinkLocationRuntimeEventKind, ZLinkSocketEventKind, ZLinkSpotEventKind } from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler, zlinkSpotTimerHandler } from '@zlink-systems/nestjs';
import { RuntimeMonitoringNames, type ProfileRes, type ProfileReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

@Injectable()
export class ProfileRequestHandler implements ZLinkRequestHandler<ProfileReq, ProfileRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ProfileReq, context: ZLinkRequestContext): Promise<ProfileRes> {
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
    await this.context.addTimer('failing', 1000, FailingTimerHandler, { stopOnUnhandledException: false });
    await this.context.addTimer('stopping', 1000, FailingTimerHandler, { stopOnUnhandledException: true });
  }
}

@Injectable()
@zlinkSpotTimerHandler()
export class FailingTimerHandler implements ZLinkSpotTimerHandler<MonitoringEntrySpot> {
  private readonly failedTimers = new Set<string>();

  async handle(_spot: MonitoringEntrySpot, tick: ZLinkTimerTick): Promise<void> {
    if (this.failedTimers.has(tick.name)) {
      return;
    }
    this.failedTimers.add(tick.name);
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
export class LocationRuntimeEventRecorder implements ZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(event: ZLinkLocationRuntimeEvent): Promise<void> {
    if (event.sourceName !== RuntimeMonitoringNames.locationRuntimeSource) {
      return;
    }
    this.evidence.add(
      `monitor-location|source=${event.sourceName}|kind=${ZLinkLocationRuntimeEventKind[event.event]}`
      + `|topology=${event.topology?.length ?? -1}|summary=${event.serviceSummary?.length ?? -1}`
      + `|storeHealthy=${event.status?.storeHealthy ?? '<none>'}`
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

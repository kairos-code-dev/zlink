import { Injectable } from '@nestjs/common';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkActorJoinRequest,
  ZLinkActorMembership,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse,
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkRuntimeEventHandler,
  ZLinkSocketEvent,
  ZLinkSpotEvent,
  ZLinkSpotTimerHandler,
  ZLinkLocationRuntimeEvent,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import { ZLinkLocationRuntimeEventKind, ZLinkSpotEventKind } from '@zlink-systems/framework';
import { zlinkRuntimeEventHandler, zlinkSpotTimerHandler } from '@zlink-systems/nestjs';
import { RuntimeMonitoringNames, socketEventName, type ProfileRes, type ProfileReq } from '../../../Shared/messages';
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
      `monitor-socket|source=${event.sourceName}|kind=${socketEventName(event.event)}`
      + `|remote=${event.remoteAddr}|routing=${event.routingId ?? '<null>'}`
    );
  }
}

@Injectable()
export class MonitoringEntrySpot implements ZLinkEntrySpot {
  declare readonly context: ZLinkEntrySpotContext;

  async onActorJoin(_actor: ZLinkActorJoinRequest, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onJoinedActor(_actor: ZLinkActorMembership): Promise<void> {}

  async onLeaveActor(_actor: ZLinkActorMembership): Promise<void> {}

  async onDisconnectActor(_actor: ZLinkActorMembership): Promise<void> {}

  async onInitialize(): Promise<void> {
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
    this.evidence.add(`monitor-spot|source=${event.sourceName}|kind=${event.event}|${spotEventDetails(event)}`);
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
      + `|${locationEventDetails(event)}`
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
    this.evidence.add(`monitor-throw|source=${event.sourceName}|kind=${event.event}`);
    throw new Error('monitoring dispatch failure for e2e');
  }
}

function spotEventDetails(event: ZLinkSpotEvent): string {
  switch (event.event) {
    case ZLinkSpotEventKind.TimerHandlerFailed:
    case ZLinkSpotEventKind.TimerStoppedAfterUnhandledException:
      return `timer=${event.timerDiagnostic.timerName}`;
  }
}

function locationEventDetails(event: ZLinkLocationRuntimeEvent): string {
  switch (event.event) {
    case ZLinkLocationRuntimeEventKind.StatusChanged:
      return `topology=-1|summary=-1|storeHealthy=${event.status.storeHealthy}`;
    case ZLinkLocationRuntimeEventKind.TopologyChanged:
      return `topology=${event.topology.length}|topologyNodes=${event.topology
        .map((entry) => `${entry.nodeRid ?? '<none>'}@${entry.endpoint ?? '<none>'}:${entry.state}`)
        .sort()
        .join(',')}|summary=-1|storeHealthy=<none>`;
    case ZLinkLocationRuntimeEventKind.ServiceSummaryChanged:
      return `topology=-1|summary=${event.serviceSummary.length}`
        + `|summaryTotal=${event.serviceSummary.reduce((total, entry) => total + entry.totalCount, 0)}`
        + `|summaryReady=${event.serviceSummary.reduce((total, entry) => total + entry.readyCount, 0)}`
        + '|storeHealthy=<none>';
    case ZLinkLocationRuntimeEventKind.StoreFailure:
    case ZLinkLocationRuntimeEventKind.StoreRecovered:
      return 'topology=-1|summary=-1|storeHealthy=<none>';
  }
}

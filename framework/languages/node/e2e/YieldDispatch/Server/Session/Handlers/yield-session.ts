import { Inject, Injectable } from '@nestjs/common';
import type {
  ActorFastMsg,
  ActorJoinYieldReq,
  ActorPushYieldReq,
  ActorYieldReq,
  BindYieldActorsRes,
  BindYieldActorsReq,
  EnsureSpotRes,
  EnsureSpotReq,
  HoldMsg,
  ProbeMsg,
  RemoteSpotYieldMsg,
  RemoteSpotYieldReq,
  TimerStartMsg,
  TimerStopMsg,
  WorkerYieldMsg,
  YieldCancelMsg,
  YieldMsg,
  YieldReq,
  YieldDispatchRes,
  YieldEvidenceRes,
  YieldEvidenceReq,
  YieldEvidenceWaitReq,
  YieldScenarioRes,
  YieldShutdownRecoveryReq,
  YieldShutdownScenarioReq,
  YieldTimeoutMsg
} from '../../../Shared/messages';
import { YieldDispatchNames } from '../../../Shared/messages';
import type {
  ZLinkMessage,
  ZLinkRouteClient,
  ZLinkSpotOutbound,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import { ZLINK_ROUTE_CLIENT, ZLINK_SPOT_OUTBOUND } from '@zlink-systems/nestjs';
import { EvidenceStore } from '../../Support/evidence-store';

class YieldSession implements ZLinkSession {
  constructor(
    private readonly route: ZLinkRouteClient,
    private readonly outbound: ZLinkSpotOutbound,
    private readonly evidence: EvidenceStore,
    readonly context: ZLinkSessionContext
  ) {}

  async onConnected(): Promise<void> {
    this.evidence.add(`session-connected|rid=${this.evidence.rid}|session=${this.context.sessionId}`);
  }

  async onDisconnected(): Promise<void> {
    this.evidence.add(`session-disconnected|rid=${this.evidence.rid}|session=${this.context.sessionId}`);
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    if (dispatch.packetName === 'EnsureSpotReq') {
      const request = payload.decode<EnsureSpotReq>(Object as never);
      const targetRid = dispatch.metadata.get(YieldDispatchNames.targetNodeRidMetadata) ?? 'play-a';
      const reply = await this.route
        .request(YieldDispatchNames.controlChannel, targetRid, request)
        .packetName('EnsureSpotReq')
        .timeout(5000)
        .submit<EnsureSpotRes>(signal);
      await this.context.client.reply(reply).submit(signal);
      return;
    }

    if (dispatch.packetName === 'BindYieldActorsReq') {
      const request = payload.decode<BindYieldActorsReq>(Object as never);
      const reply = await this.route
        .request(YieldDispatchNames.controlChannel, 'play-a', request)
        .packetName('BindYieldActorsReq')
        .timeout(5000)
        .submit<BindYieldActorsRes>(signal);
      for (const actor of reply.actors) {
        await this.context.actors.bind({
          actorId: actor.actorId,
          nodeRid: actor.nodeRid,
          generation: BigInt(actor.generation)
        }, signal);
      }
      await this.context.client.reply(reply).submit(signal);
      return;
    }

    if (dispatch.packetName === 'YieldEvidenceReq') {
      const request = payload.decode<YieldEvidenceReq>(Object as never);
      const targetRid = dispatch.metadata.get(YieldDispatchNames.targetNodeRidMetadata) ?? 'play-a';
      const reply = await this.route
        .request(YieldDispatchNames.controlChannel, targetRid, request)
        .packetName('YieldEvidenceReq')
        .timeout(5000)
        .submit<YieldEvidenceRes>(signal);
      await this.context.client.reply(reply).submit(signal);
      return;
    }

    if (dispatch.packetName === 'YieldEvidenceWaitReq') {
      const request = payload.decode<YieldEvidenceWaitReq>(Object as never);
      const targetRid = dispatch.metadata.get(YieldDispatchNames.targetNodeRidMetadata) ?? 'play-a';
      const reply = await this.route
        .request(YieldDispatchNames.controlChannel, targetRid, request)
        .packetName('YieldEvidenceWaitReq')
        .timeout(request.timeoutMilliseconds ?? 30000)
        .submit<YieldEvidenceRes>(signal);
      await this.context.client.reply(reply).submit(signal);
      return;
    }

    if (dispatch.packetName === 'YieldShutdownScenarioReq') {
      const request = payload.decode<YieldShutdownScenarioReq>(Object as never);
      this.evidence.add(
        `session-shutdown|rid=${this.evidence.rid}|session=${this.context.sessionId}`
        + `|request=${request.requestId}|spot=${request.spotRid}`
      );
      const result = await this.runShutdownThroughSpotRoute(request, signal);
      await this.context.client.reply(result).submit(signal);
      return;
    }

    if (dispatch.packetName === 'YieldShutdownRecoveryReq') {
      const request = payload.decode<YieldShutdownRecoveryReq>(Object as never);
      this.evidence.add(
        `session-shutdown-recovery|rid=${this.evidence.rid}|session=${this.context.sessionId}`
        + `|request=${request.requestId}|spot=${request.spotRid}`
      );
      const result = await this.runShutdownRecoveryThroughSpotRoute(request, signal);
      await this.context.client.reply(result).submit(signal);
      return;
    }

    if (dispatch.packetName === 'HoldMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<HoldMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'YieldMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<YieldMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'WorkerYieldMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<WorkerYieldMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'YieldTimeoutMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<YieldTimeoutMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'YieldCancelMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<YieldCancelMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'ProbeMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<ProbeMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'TimerStartMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<TimerStartMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'TimerStopMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<TimerStopMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'RemoteSpotYieldReq') {
      const reply = await this.relayToSpotRequestWithRetry(
        dispatch,
        payload.decode<RemoteSpotYieldReq>(Object as never),
        signal
      );
      await this.context.client.reply(reply).submit(signal);
      return;
    }

    if (dispatch.packetName === 'RemoteSpotYieldMsg') {
      await this.relayToSpotWithRetry(dispatch, payload.decode<RemoteSpotYieldMsg>(Object as never), signal);
      return;
    }

    if (dispatch.packetName === 'ActorYieldReq') {
      payload.decode<ActorYieldReq>(Object as never);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    if (dispatch.packetName === 'ActorFastMsg') {
      payload.decode<ActorFastMsg>(Object as never);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    if (dispatch.packetName === 'ActorJoinYieldReq') {
      payload.decode<ActorJoinYieldReq>(Object as never);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    if (dispatch.packetName === 'ActorPushYieldReq') {
      payload.decode<ActorPushYieldReq>(Object as never);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    throw new Error(`Unsupported YieldDispatch stream packet '${dispatch.packetName}'.`);
  }

  private async relayToActor(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    const actorId = dispatch.metadata.get(YieldDispatchNames.actorIdMetadata);
    const actor = actorId === undefined || actorId.trim() === ''
      ? this.requireSingleBoundActor(dispatch.packetName)
      : this.context.actors.find(actorId);
    if (actor === undefined) {
      throw new Error(`Actor route not found: ${actorId}`);
    }
    await actor.relay(payload, signal);
  }

  private requireSingleBoundActor(packetName: string) {
    if (this.context.actors.bound.length === 1) {
      return this.context.actors.bound[0];
    }
    if (this.context.actors.bound.length === 0) {
      throw new Error(`No actor is bound for packet '${packetName}'.`);
    }
    throw new Error(`Actor id metadata is required for packet '${packetName}' with multiple bound actors.`);
  }

  private async runShutdownThroughSpotRoute(
    request: YieldShutdownScenarioReq,
    signal?: AbortSignal
  ): Promise<YieldScenarioRes> {
    await this.ensurePlaySpot(request.spotRid, signal);
    await this.outbound
      .requestToSpot(request.spotRid, {
        requestId: request.requestId,
        delayMs: request.delayMs,
        correlationId: 'shutdown'
      } satisfies YieldReq)
      .packetName('YieldReq')
      .timeout(90000)
      .submit<YieldDispatchRes>(signal);
    const evidence = await this.requestPlayEvidence(request.requestId, signal);
    return {
      operation: 'yield.e3-shutdown-unexpected-completion',
      spotRid: request.spotRid,
      evidence: evidence.evidence
    };
  }

  private async runShutdownRecoveryThroughSpotRoute(
    request: YieldShutdownRecoveryReq,
    signal?: AbortSignal
  ): Promise<YieldScenarioRes> {
    await this.ensurePlaySpot(request.spotRid, signal);
    await this.outbound
      .sendToSpot(request.spotRid, {
        requestId: request.requestId,
        marker: 'shutdown-recovery-probe'
      } satisfies ProbeMsg)
      .packetName('ProbeMsg')
      .submit(signal);
    await this.requestPlayEvidenceWait(request.requestId, 'probe-completed', signal);
    const evidence = await this.requestPlayEvidence(request.requestId, signal);
    if (!evidence.evidence.some((line) =>
      line.includes('probe-completed')
      && line.includes(`rid=play-a|spot=${request.spotRid}`)
      && line.includes('marker=shutdown-recovery-probe')
    )) {
      throw new Error('YD-E3 recovery probe marker missing.');
    }
    return {
      operation: 'yield.e3-shutdown-recovery',
      spotRid: request.spotRid,
      evidence: evidence.evidence
    };
  }

  private async ensurePlaySpot(spotRid: string, signal?: AbortSignal): Promise<void> {
    await this.route
      .request(YieldDispatchNames.controlChannel, 'play-a', { spotRid } satisfies EnsureSpotReq)
      .packetName('EnsureSpotReq')
      .timeout(5000)
      .submit<EnsureSpotRes>(signal);
  }

  private async requestPlayEvidence(requestId: string, signal?: AbortSignal): Promise<YieldEvidenceRes> {
    return await this.route
      .request(YieldDispatchNames.controlChannel, 'play-a', { requestId } satisfies YieldEvidenceReq)
      .packetName('YieldEvidenceReq')
      .timeout(5000)
      .submit<YieldEvidenceRes>(signal);
  }

  private async requestPlayEvidenceWait(
    requestId: string,
    marker: string,
    signal?: AbortSignal
  ): Promise<YieldEvidenceRes> {
    return await this.route
      .request(YieldDispatchNames.controlChannel, 'play-a', { requestId, marker, timeoutMilliseconds: 20000 } satisfies YieldEvidenceWaitReq)
      .packetName('YieldEvidenceWaitReq')
      .timeout(20000)
      .submit<YieldEvidenceRes>(signal);
  }

  private async relayToSpotWithRetry(
    dispatch: ZLinkSessionDispatchContext,
    request: HoldMsg | YieldMsg | WorkerYieldMsg | YieldTimeoutMsg | YieldCancelMsg | ProbeMsg | TimerStartMsg | TimerStopMsg | RemoteSpotYieldMsg,
    signal?: AbortSignal
  ): Promise<void> {
    const spotRid = dispatch.metadata.get(YieldDispatchNames.spotRidMetadata);
    if (spotRid === undefined || spotRid.trim() === '') {
      throw new Error(`${YieldDispatchNames.spotRidMetadata} metadata is required for '${dispatch.packetName}'.`);
    }
    const deadline = Date.now() + 10000;
    let lastError: unknown;
    while (Date.now() < deadline) {
      try {
        await this.outbound
          .sendToSpot(spotRid, request)
          .packetName(dispatch.packetName)
          .submit(signal);
        this.evidence.add(`spot-relay|rid=${this.evidence.rid}|spot=${spotRid}|packet=${dispatch.packetName}|status=sent`);
        return;
      } catch (error) {
        lastError = error;
        await new Promise((resolve) => setTimeout(resolve, 100));
      }
    }
    const message = lastError instanceof Error ? lastError.message : String(lastError);
    this.evidence.add(`spot-relay|rid=${this.evidence.rid}|spot=${spotRid}|packet=${dispatch.packetName}|status=failed|error=${message}`);
    throw new Error(`Timed out relaying '${dispatch.packetName}' to spot '${spotRid}'. Last error: ${message}`);
  }

  private async relayToSpotRequestWithRetry(
    dispatch: ZLinkSessionDispatchContext,
    request: RemoteSpotYieldReq,
    signal?: AbortSignal
  ): Promise<YieldDispatchRes> {
    const spotRid = dispatch.metadata.get(YieldDispatchNames.spotRidMetadata);
    if (spotRid === undefined || spotRid.trim() === '') {
      throw new Error(`${YieldDispatchNames.spotRidMetadata} metadata is required for '${dispatch.packetName}'.`);
    }
    const deadline = Date.now() + 10000;
    let lastError: unknown;
    while (Date.now() < deadline) {
      try {
        const reply = await this.outbound
          .requestToSpot(spotRid, request)
          .packetName(dispatch.packetName)
          .timeout(5000)
          .submit<YieldDispatchRes>(signal);
        this.evidence.add(`spot-relay|rid=${this.evidence.rid}|spot=${spotRid}|packet=${dispatch.packetName}|status=replied`);
        return reply;
      } catch (error) {
        lastError = error;
        await new Promise((resolve) => setTimeout(resolve, 100));
      }
    }
    const message = lastError instanceof Error ? lastError.message : String(lastError);
    this.evidence.add(`spot-relay|rid=${this.evidence.rid}|spot=${spotRid}|packet=${dispatch.packetName}|status=failed|error=${message}`);
    throw new Error(`Timed out relaying '${dispatch.packetName}' to spot '${spotRid}'. Last error: ${message}`);
  }
}

@Injectable()
export class YieldSessionFactory implements ZLinkSessionFactory<YieldSession> {
  constructor(
    @Inject(ZLINK_ROUTE_CLIENT) private readonly route: ZLinkRouteClient,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    private readonly evidence: EvidenceStore
  ) {}

  create(context: ZLinkSessionContext): YieldSession {
    return new YieldSession(this.route, this.outbound, this.evidence, context);
  }
}

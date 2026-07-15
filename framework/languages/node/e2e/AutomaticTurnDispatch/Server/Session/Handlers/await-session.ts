import { Inject, Injectable } from '@nestjs/common';
import {
  ActorFastMsg,
  ActorFastReq,
  ActorJoinAwaitReq,
  ActorPushAwaitReq,
  ActorAwaitReq,
  BindAwaitActorsRes,
  BindAwaitActorsReq,
  EnsureSpotRes,
  EnsureSpotReq,
  HoldMsg,
  ProbeMsg,
  RemoteSpotAwaitMsg,
  RemoteSpotAwaitReq,
  TimerStartMsg,
  TimerStopMsg,
  WorkerAwaitMsg,
  AwaitCancelMsg,
  AwaitMsg,
  AwaitReq,
  AutomaticTurnDispatchRes,
  AwaitEvidenceRes,
  AwaitEvidenceReq,
  AwaitEvidenceWaitReq,
  AwaitScenarioRes,
  AwaitShutdownRecoveryReq,
  AwaitShutdownScenarioReq,
  AwaitTimeoutMsg,
  CounterResetMsg,
  CounterAwaitMsg,
  CounterReadReq,
  CounterReadRes,
  HttpAwaitMsg,
  IoWorkerBatchReq,
  IoWorkerBatchRes,
  CpuWorkerAwaitMsg,
  SelfCycleMsg,
  ProbeReq
} from '../../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../../Shared/messages';
import type {
  ZLinkMessage,
  ZLinkRouteClient,
  ZLinkSpotOutbound,
  ZLinkSpotHandleResolver,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import { ZLINK_ROUTE_CLIENT, ZLINK_SPOT_OUTBOUND, ZLINK_SPOT_HANDLE_RESOLVER } from '@zlink-systems/nestjs';
import { EvidenceStore } from '../Support/evidence-store';

class AwaitSession implements ZLinkSession {
  constructor(
    private readonly route: ZLinkRouteClient,
    private readonly outbound: ZLinkSpotOutbound,
    private readonly spotHandles: ZLinkSpotHandleResolver,
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
      const request = decodePacket(payload, EnsureSpotReq);
      const targetRid = dispatch.metadata.get(AutomaticTurnDispatchNames.targetNodeRidMetadata) ?? 'play-a';
      const reply = await this.route
        .requestToNode(AutomaticTurnDispatchNames.controlChannel, targetRid, request)
        .timeout(5000)
        .submit<EnsureSpotRes>(signal);
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'BindAwaitActorsReq') {
      const request = decodePacket(payload, BindAwaitActorsReq);
      const reply = await this.route
        .requestToNode(AutomaticTurnDispatchNames.controlChannel, 'play-a', request)
        .timeout(5000)
        .submit<BindAwaitActorsRes>(signal);
      for (const actor of reply.actors) {
        await this.context.actors.bindOrGet({
          actorId: actor.actorId,
          nodeRid: actor.nodeRid,
          generation: BigInt(actor.generation)
        }, signal);
      }
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'AwaitEvidenceReq') {
      const request = decodePacket(payload, AwaitEvidenceReq);
      const targetRid = dispatch.metadata.get(AutomaticTurnDispatchNames.targetNodeRidMetadata) ?? 'play-a';
      const reply = await this.route
        .requestToNode(AutomaticTurnDispatchNames.controlChannel, targetRid, request)
        .timeout(5000)
        .submit<AwaitEvidenceRes>(signal);
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'AwaitEvidenceWaitReq') {
      const request = decodePacket(payload, AwaitEvidenceWaitReq);
      const targetRid = dispatch.metadata.get(AutomaticTurnDispatchNames.targetNodeRidMetadata) ?? 'play-a';
      const reply = await this.route
        .requestToNode(AutomaticTurnDispatchNames.controlChannel, targetRid, request)
        .timeout(request.timeoutMilliseconds ?? 30000)
        .submit<AwaitEvidenceRes>(signal);
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'AwaitShutdownScenarioReq') {
      const request = decodePacket(payload, AwaitShutdownScenarioReq);
      this.evidence.add(
        `session-shutdown|rid=${this.evidence.rid}|session=${this.context.sessionId}`
        + `|request=${request.requestId}|spot=${request.spotRid}`
      );
      const result = await this.runShutdownThroughSpotRoute(request, signal);
      this.context.client.reply(result).submit();
      return;
    }

    if (dispatch.packetName === 'AwaitShutdownRecoveryReq') {
      const request = decodePacket(payload, AwaitShutdownRecoveryReq);
      this.evidence.add(
        `session-shutdown-recovery|rid=${this.evidence.rid}|session=${this.context.sessionId}`
        + `|request=${request.requestId}|spot=${request.spotRid}`
      );
      const result = await this.runShutdownRecoveryThroughSpotRoute(request, signal);
      this.context.client.reply(result).submit();
      return;
    }

    if (dispatch.packetName === 'HoldMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, HoldMsg), signal);
      return;
    }

    if (dispatch.packetName === 'AwaitMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, AwaitMsg), signal);
      return;
    }

    if (dispatch.packetName === 'WorkerAwaitMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, WorkerAwaitMsg), signal);
      return;
    }

    if (dispatch.packetName === 'AwaitTimeoutMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, AwaitTimeoutMsg), signal);
      return;
    }

    if (dispatch.packetName === 'AwaitCancelMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, AwaitCancelMsg), signal);
      return;
    }

    if (dispatch.packetName === 'ProbeMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, ProbeMsg), signal);
      return;
    }

    if (dispatch.packetName === 'CounterResetMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, CounterResetMsg), signal);
      return;
    }

    if (dispatch.packetName === 'CounterAwaitMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, CounterAwaitMsg), signal);
      return;
    }

    if (dispatch.packetName === 'HttpAwaitMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, HttpAwaitMsg), signal);
      return;
    }

    if (dispatch.packetName === 'IoWorkerBatchReq') {
      const reply = await this.relayToSpotRequest<IoWorkerBatchRes>(
        dispatch,
        decodePacket(payload, IoWorkerBatchReq),
        signal
      );
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'CpuWorkerAwaitMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, CpuWorkerAwaitMsg), signal);
      return;
    }

    if (dispatch.packetName === 'SelfCycleMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, SelfCycleMsg), signal);
      return;
    }

    if (dispatch.packetName === 'CounterReadReq') {
      const reply = await this.relayToSpotRequest<CounterReadRes>(
        dispatch,
        decodePacket(payload, CounterReadReq),
        signal
      );
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'ProbeReq') {
      const reply = await this.relayToSpotRequest<AutomaticTurnDispatchRes>(
        dispatch,
        decodePacket(payload, ProbeReq),
        signal
      );
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'TimerStartMsg') {
      await this.relayTimerControl(dispatch, decodePacket(payload, TimerStartMsg), signal);
      return;
    }

    if (dispatch.packetName === 'TimerStopMsg') {
      await this.relayTimerControl(dispatch, decodePacket(payload, TimerStopMsg), signal);
      return;
    }

    if (dispatch.packetName === 'RemoteSpotAwaitReq') {
      const reply = await this.relayToSpotRequest(
        dispatch,
        decodePacket(payload, RemoteSpotAwaitReq),
        signal
      );
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'AwaitReq') {
      const reply = await this.relayToSpotRequest(
        dispatch,
        decodePacket(payload, AwaitReq),
        signal
      );
      this.context.client.reply(reply).submit();
      return;
    }

    if (dispatch.packetName === 'RemoteSpotAwaitMsg') {
      await this.relayToSpot(dispatch, decodePacket(payload, RemoteSpotAwaitMsg), signal);
      return;
    }

    if (dispatch.packetName === 'ActorAwaitReq') {
      decodePacket(payload, ActorAwaitReq);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    if (dispatch.packetName === 'ActorFastMsg') {
      decodePacket(payload, ActorFastMsg);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    if (dispatch.packetName === 'ActorFastReq') {
      decodePacket(payload, ActorFastReq);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    if (dispatch.packetName === 'ActorJoinAwaitReq') {
      decodePacket(payload, ActorJoinAwaitReq);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    if (dispatch.packetName === 'ActorPushAwaitReq') {
      decodePacket(payload, ActorPushAwaitReq);
      await this.relayToActor(dispatch, payload, signal);
      return;
    }

    throw new Error(`Unsupported AutomaticTurnDispatch stream packet '${dispatch.packetName}'.`);
  }

  private async relayToActor(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    const actorId = dispatch.metadata.get(AutomaticTurnDispatchNames.actorIdMetadata);
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
    request: AwaitShutdownScenarioReq,
    signal?: AbortSignal
  ): Promise<AwaitScenarioRes> {
    await this.ensurePlaySpot(request.spotRid, signal);
    const spot = await this.requireSpotHandle(request.spotRid, signal);
    await this.outbound
      .requestToSpot(spot, Object.assign(new AwaitReq(), {
        requestId: request.requestId,
        delayMs: request.delayMs,
        correlationId: 'shutdown'
      }))
      .timeout(90000)
      .submit<AutomaticTurnDispatchRes>(signal);
    const evidence = await this.requestPlayEvidence(request.requestId, signal);
    return {
      operation: 'await.e3-shutdown-unexpected-completion',
      spotRid: request.spotRid,
      evidence: evidence.evidence
    };
  }

  private async runShutdownRecoveryThroughSpotRoute(
    request: AwaitShutdownRecoveryReq,
    signal?: AbortSignal
  ): Promise<AwaitScenarioRes> {
    await this.ensurePlaySpot(request.spotRid, signal);
    const spot = await this.requireSpotHandle(request.spotRid, signal);
    await this.outbound
      .sendToSpot(spot, Object.assign(new ProbeMsg(), {
        requestId: request.requestId,
        marker: 'shutdown-recovery-probe'
      }))
      .submit();
    await this.requestPlayEvidenceWait(request.requestId, 'probe-completed', signal);
    const evidence = await this.requestPlayEvidence(request.requestId, signal);
    if (!evidence.evidence.some((line) =>
      line.includes('probe-completed')
      && line.includes(`rid=play-a|spot=${request.spotRid}`)
      && line.includes('marker=shutdown-recovery-probe')
    )) {
      throw new Error('TD-F5 recovery probe marker missing.');
    }
    return {
      operation: 'await.e3-shutdown-recovery',
      spotRid: request.spotRid,
      evidence: evidence.evidence
    };
  }

  private async ensurePlaySpot(spotRid: string, signal?: AbortSignal): Promise<void> {
    await this.route
      .requestToNode(
        AutomaticTurnDispatchNames.controlChannel,
        'play-a',
        Object.assign(new EnsureSpotReq(), { spotRid })
      )
      .timeout(5000)
      .submit<EnsureSpotRes>(signal);
  }

  private async requestPlayEvidence(requestId: string, signal?: AbortSignal): Promise<AwaitEvidenceRes> {
    return await this.route
      .requestToNode(
        AutomaticTurnDispatchNames.controlChannel,
        'play-a',
        Object.assign(new AwaitEvidenceReq(), { requestId })
      )
      .timeout(5000)
      .submit<AwaitEvidenceRes>(signal);
  }

  private async requestPlayEvidenceWait(
    requestId: string,
    marker: string,
    signal?: AbortSignal
  ): Promise<AwaitEvidenceRes> {
    return await this.route
      .requestToNode(
        AutomaticTurnDispatchNames.controlChannel,
        'play-a',
        Object.assign(new AwaitEvidenceWaitReq(), { requestId, marker, timeoutMilliseconds: 20000 })
      )
      .timeout(20000)
      .submit<AwaitEvidenceRes>(signal);
  }

  private async relayToSpot(
    dispatch: ZLinkSessionDispatchContext,
    request: HoldMsg | AwaitMsg | WorkerAwaitMsg | AwaitTimeoutMsg | AwaitCancelMsg | ProbeMsg
      | CounterResetMsg | CounterAwaitMsg | HttpAwaitMsg | CpuWorkerAwaitMsg | SelfCycleMsg
      | TimerStartMsg | TimerStopMsg | RemoteSpotAwaitMsg,
    signal?: AbortSignal
  ): Promise<void> {
    const spotRid = dispatch.metadata.get(AutomaticTurnDispatchNames.spotRidMetadata);
    if (spotRid === undefined || spotRid.trim() === '') {
      throw new Error(`${AutomaticTurnDispatchNames.spotRidMetadata} metadata is required for '${dispatch.packetName}'.`);
    }
    const spot = await this.requireSpotHandle(spotRid, signal);
    await this.outbound
      .sendToSpot(spot, request)
      .submit();
    this.evidence.add(`spot-relay|rid=${this.evidence.rid}|spot=${spotRid}|packet=${dispatch.packetName}|status=sent`);
  }

  private async relayToSpotRequest<TReply = AutomaticTurnDispatchRes>(
    dispatch: ZLinkSessionDispatchContext,
    request: RemoteSpotAwaitReq | AwaitReq | CounterReadReq | ProbeReq | IoWorkerBatchReq,
    signal?: AbortSignal
  ): Promise<TReply> {
    const spotRid = dispatch.metadata.get(AutomaticTurnDispatchNames.spotRidMetadata);
    if (spotRid === undefined || spotRid.trim() === '') {
      throw new Error(`${AutomaticTurnDispatchNames.spotRidMetadata} metadata is required for '${dispatch.packetName}'.`);
    }
    const spot = await this.requireSpotHandle(spotRid, signal);
    const reply = await this.outbound
      .requestToSpot(spot, request)
      .timeout(5000)
      .submit<TReply>(signal);
    this.evidence.add(`spot-relay|rid=${this.evidence.rid}|spot=${spotRid}|packet=${dispatch.packetName}|status=replied`);
    return reply;
  }

  private async relayTimerControl(
    dispatch: ZLinkSessionDispatchContext,
    request: TimerStartMsg | TimerStopMsg,
    signal?: AbortSignal
  ): Promise<void> {
    if (!dispatch.canReply) {
      await this.relayToSpot(dispatch, request, signal);
      return;
    }
    const spotRid = dispatch.metadata.get(AutomaticTurnDispatchNames.spotRidMetadata);
    if (spotRid === undefined || spotRid.trim() === '') {
      throw new Error(`${AutomaticTurnDispatchNames.spotRidMetadata} metadata is required for '${dispatch.packetName}'.`);
    }
    const spot = await this.requireSpotHandle(spotRid, signal);
    await this.outbound
      .requestToSpot(spot, request)
      .timeout(5000)
      .submit(signal);
    this.evidence.add(`spot-relay|rid=${this.evidence.rid}|spot=${spotRid}|packet=${dispatch.packetName}|status=replied`);
    this.context.client.reply({ ok: true }).submit();
  }

  private async requireSpotHandle(spotRid: string, signal?: AbortSignal) {
    const spot = await this.spotHandles.resolveSpotHandle(spotRid, signal);
    if (spot === undefined) {
      throw new Error(`SpotHandle '${spotRid}' was not found.`);
    }
    return spot;
  }

}

function decodePacket<T extends object>(payload: ZLinkMessage, type: new () => T): T {
  return Object.assign(new type(), payload.decode<T>(Object as never));
}

@Injectable()
export class AwaitSessionFactory implements ZLinkSessionFactory<AwaitSession> {
  constructor(
    @Inject(ZLINK_ROUTE_CLIENT) private readonly route: ZLinkRouteClient,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    private readonly evidence: EvidenceStore
  ) {}

  async create(context: ZLinkSessionContext): Promise<AwaitSession> {
    return new AwaitSession(this.route, this.outbound, this.spotHandles, this.evidence, context);
  }
}

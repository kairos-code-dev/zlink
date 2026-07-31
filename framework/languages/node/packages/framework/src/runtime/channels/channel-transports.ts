import {
  Message,
  RequestError,
  SubmitError,
  SubmitResult,
  type MessageLike
} from '@zlink-systems/zlink';
import type {
  ZLinkBackendMeshNode,
  ZLinkBackendSpot
} from '../backend/contracts';
import type {
  ZLinkFrameworkErrorKind as ZLinkFrameworkErrorKindType
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import {
  ZLinkSubmitStatus,
  type ZLinkSubmitResult
} from '../messaging/submission-result';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import { ZLinkSpotKind } from '../../contracts/Spots';
import { throwIfAborted } from '../abort';
import type { ZLinkRuntimeMetrics } from '../diagnostics';
import {
  closeMeshCompletion,
  type ZLinkMeshCompletionTable
} from '../backend/mesh-completion-table';
import { routingIdsEqual, toBindingRoutingId } from '../routing-id';
import {
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  type ZLinkChannelEnvelopeCodecRegistry,
  ZLinkChannelMessageKind
} from './channel-envelope';
import type { ZLinkMeshSubmitterRegistry } from '../messaging';
import type { ServiceDirectSpotRouteFence } from '../foundation/service-stateful-wire-codec';

export interface ZLinkChannelClientTransport {
  trySend?(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  send(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): void | ZLinkSubmitResult | Promise<void | ZLinkSubmitResult>;
  request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply>;
  tryPublish?(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult | Promise<ZLinkSubmitResult>;
}

export interface ZLinkSpotPublisherClientTransport {
  tryPublish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  publish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult | Promise<ZLinkSubmitResult>;
}

export interface ZLinkRouteClientTransport {
  trySubmit?(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  submit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): void | ZLinkSubmitResult | Promise<void | ZLinkSubmitResult>;
  request<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply>;
  trySubmitToChannel?(
    meshName: string,
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  submitToChannel(
    meshName: string,
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): void | ZLinkSubmitResult | Promise<void | ZLinkSubmitResult>;
  requestToChannel<TReply>(
    meshName: string,
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply>;
  sendToSpot?(
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: {
      readonly packetName?: string;
      readonly signal?: AbortSignal;
      readonly metadata?: ReadonlyMap<string, string>;
    }
  ): Promise<ZLinkSubmitResult>;
  requestToSpot?<TReply = unknown>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: {
      readonly packetName?: string;
      readonly timeoutMs?: number;
      readonly signal?: AbortSignal;
      readonly metadata?: ReadonlyMap<string, string>;
    }
  ): Promise<TReply>;
}

interface ZLinkChannelTransportRuntime {
  trySend(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  send(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult>;
  request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply>;
  tryPublish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult>;
  canRouteChannel(routerChannelId: string): boolean;
  canRoutePacketChannel(routerChannelId: string): boolean;
  tryRouteSubmit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult;
  routeSubmit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult>;
  routeRequest<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply>;
  routeSendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<void>;
  routeSendFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<void>;
  routeRequestToSpot<TReply>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply>;
  routeRequestFromSpotToSpot<TReply>(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  routeRequestRawFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]>;
  routeRequestRawToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]>;
}

export class ZLinkRuntimeChannelTransport implements ZLinkChannelClientTransport {
  constructor(private readonly manager: () => ZLinkChannelTransportRuntime | undefined) {}

  trySend(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult {
    return this.requireManager().trySend(channelName, packetName, message, metadata);
  }

  send(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult> {
    return this.requireManager().send(channelName, packetName, message, signal, metadata);
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply> {
    return this.requireManager().request(channelName, packetName, request, timeoutMs, signal, metadata);
  }

  tryPublish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult {
    return this.requireManager().tryPublish(channelName, topic, packetName, event, metadata);
  }

  publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult> {
    return this.requireManager().publish(channelName, topic, packetName, event, signal, metadata);
  }

  private requireManager(): ZLinkChannelTransportRuntime {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return manager;
  }
}

export class ZLinkRuntimeRouteTransport implements ZLinkRouteClientTransport {
  constructor(
    private readonly manager: () => ZLinkChannelTransportRuntime | undefined,
    private readonly routeChannelPredicate: ((routerChannelId: string) => boolean) | undefined = undefined,
    private readonly meshRuntime: (() => {
      readonly meshNode: (meshName: string) => ZLinkBackendMeshNode | undefined;
      readonly meshCompletionTable: (meshName: string) => ZLinkMeshCompletionTable | undefined;
    } | undefined) | undefined = undefined,
    private readonly codecs?: ZLinkChannelEnvelopeCodecRegistry,
    private readonly meshSubmitters?: ZLinkMeshSubmitterRegistry,
    private readonly manualNodeTarget?: (meshName: string, targetNodeRid: string) => boolean | undefined,
    private readonly localNodeSubmit?: (
      meshName: string,
      sourceNodeRid: string,
      parts: readonly MessageLike[]
    ) => ZLinkSubmitResult,
    private readonly metrics?: ZLinkRuntimeMetrics
  ) {}

  canRouteChannel(routerChannelId: string): boolean {
    const manager = this.manager();
    if (manager !== undefined) {
      return manager.canRouteChannel(routerChannelId);
    }
    return this.routeChannelPredicate?.(routerChannelId) ?? false;
  }

  canRoutePacketChannel(routerChannelId: string): boolean {
    return this.manager()?.canRoutePacketChannel(routerChannelId)
      ?? this.routeChannelPredicate?.(routerChannelId)
      ?? false;
  }

  trySubmit(
    meshName: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult {
    const node = this.meshNode(meshName);
    if (node === undefined) {
      return this.requireManager().tryRouteSubmit(meshName, targetNodeRid, packetName, message, metadata);
    }
    if (node.isObjectClientNodeDirectTarget?.(toBindingRoutingId(targetNodeRid)) === true) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    const parts = this.encodeMessage(
      ZLinkChannelMessageKind.Command,
      meshName,
      packetName,
      message,
      undefined,
      metadata
    );
    if (this.isSelfNode(node, targetNodeRid)) {
      return this.submitLocalNode(meshName, String(node.status().routingId), parts);
    }
    if (!this.isKnownBackendPeer(node, targetNodeRid)
      && this.manualNodeTarget?.(meshName, targetNodeRid) === false) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    return mapMeshSubmitResult(node.sendToNode(toBindingRoutingId(targetNodeRid), parts, { flags: 1 }));
  }

  async submit(
    meshName: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult> {
    return this.submitNode(
      meshName,
      targetNodeRid,
      packetName,
      message,
      signal,
      metadata,
      false
    );
  }

  async submitInfrastructure(
    meshName: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult> {
    return this.submitNode(
      meshName,
      targetNodeRid,
      packetName,
      message,
      signal,
      metadata,
      true
    );
  }

  private async submitNode(
    meshName: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal: AbortSignal | undefined,
    metadata: ReadonlyMap<string, string> | undefined,
    allowObjectClientTarget: boolean
  ): Promise<ZLinkSubmitResult> {
    const node = this.meshNode(meshName);
    if (node === undefined) {
      return await this.requireManager().routeSubmit(
        meshName,
        targetNodeRid,
        packetName,
        message,
        signal,
        metadata
      );
    }
    throwIfAborted(signal);
    if (!allowObjectClientTarget
      && node.isObjectClientNodeDirectTarget?.(toBindingRoutingId(targetNodeRid)) === true) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    const parts = this.encodeMessage(
      ZLinkChannelMessageKind.Command,
      meshName,
      packetName,
      message,
      undefined,
      metadata
    );
    const operation = `MeshNode '${meshName}' send to node '${targetNodeRid}'`;
    if (this.isSelfNode(node, targetNodeRid)) {
      return await this.requireMeshSubmitters().submit(
        meshName,
        () => this.submitLocalNode(meshName, String(node.status().routingId), parts),
        signal
      );
    }
    if (!this.isKnownBackendPeer(node, targetNodeRid)
      && this.manualNodeTarget?.(meshName, targetNodeRid) === false) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    return await this.requireMeshSubmitters().submit(meshName, () => {
      try {
        return mapMeshSubmitResult(
          node.sendToNode(toBindingRoutingId(targetNodeRid), parts, { flags: 1 })
        );
      } catch (error) {
        throw mapMeshSubmissionError(error, operation);
      }
    }, signal);
  }

  async request<TReply>(
    meshName: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply> {
    const node = this.meshNode(meshName);
    if (node === undefined) {
      return this.requireManager().routeRequest(
        meshName,
        targetNodeRid,
        packetName,
        request,
        timeoutMs,
        signal,
        metadata
      );
    }
    throwIfAborted(signal);
    if (node.isObjectClientNodeDirectTarget?.(toBindingRoutingId(targetNodeRid)) === true) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestTargetNotFound,
        `MeshNode '${meshName}' target '${targetNodeRid}' is an Object Client.`
      );
    }
    const parts = this.encodeMessage(
      ZLinkChannelMessageKind.Request,
      meshName,
      packetName,
      request,
      timeoutMs,
      metadata
    );
    let operationId;
    try {
      operationId = node.requestToNode(
        toBindingRoutingId(targetNodeRid),
        parts,
        { flags: 1, timeoutMs }
      );
    } catch (error) {
      throw mapMeshSubmissionError(
        error,
        `MeshNode '${meshName}' request to node '${targetNodeRid}'`
      );
    }
    return this.waitForMeshReply(meshName, operationId, signal);
  }

  trySubmitToChannel(
    meshName: string,
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    metadata?: ReadonlyMap<string, string>
  ): ZLinkSubmitResult {
    const node = this.requireMeshNode(meshName);
    const result = mapMeshSubmitResult(node.sendToChannel(
      channelName,
      this.encodeMessage(
        ZLinkChannelMessageKind.Command,
        channelName,
        packetName,
        message,
        undefined,
        metadata
      ),
      { flags: 1 }
    ));
    if (result.status === ZLinkSubmitStatus.TargetNotFound) {
      this.metrics?.recordChannelSelectionFailure(meshName, channelName, 'no_ready_target');
    }
    return result;
  }

  async submitToChannel(
    meshName: string,
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<ZLinkSubmitResult> {
    throwIfAborted(signal);
    const node = this.requireMeshNode(meshName);
    const parts = this.encodeMessage(
      ZLinkChannelMessageKind.Command,
      channelName,
      packetName,
      message,
      undefined,
      metadata
    );
    const operation = `MeshNode '${meshName}' send to channel '${channelName}'`;
    return await this.requireMeshSubmitters().submit(meshName, () => {
      try {
        const result = mapMeshSubmitResult(node.sendToChannel(channelName, parts, { flags: 1 }));
        if (result.status === ZLinkSubmitStatus.TargetNotFound) {
          this.metrics?.recordChannelSelectionFailure(meshName, channelName, 'no_ready_target');
        }
        return result;
      } catch (error) {
        throw mapMeshSubmissionError(error, operation);
      }
    }, signal);
  }

  async requestToChannel<TReply>(
    meshName: string,
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply> {
    throwIfAborted(signal);
    const node = this.requireMeshNode(meshName);
    const parts = this.encodeMessage(
      ZLinkChannelMessageKind.Request,
      channelName,
      packetName,
      request,
      timeoutMs,
      metadata
    );
    const metric = this.metrics?.startRequest(meshName, 'channel');
    let operationId;
    try {
      operationId = node.requestToChannel(channelName, parts, { flags: 1, timeoutMs });
    } catch (error) {
      metric?.complete(requestMetricOutcome(error));
      if (requestMetricOutcome(error) === 'target_not_found') {
        this.metrics?.recordChannelSelectionFailure(meshName, channelName, 'no_ready_target');
      }
      throw mapMeshSubmissionError(
        error,
        `MeshNode '${meshName}' request to channel '${channelName}'`
      );
    }
    try {
      const reply = await this.waitForMeshReply<TReply>(meshName, operationId, signal);
      metric?.complete('completed');
      return reply;
    } catch (error) {
      metric?.complete(requestMetricOutcome(error));
      throw error;
    }
  }

  async sendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: {
      readonly packetName?: string;
      readonly signal?: AbortSignal;
      readonly metadata?: ReadonlyMap<string, string>;
    }
  ): Promise<ZLinkSubmitResult> {
    const node = this.meshNode(spotRouteTarget.routerChannelId);
    if (node === undefined) {
      await this.requireManager().routeSendToSpot(
        spotRouteTarget,
        options.packetName,
        message,
        options.signal,
        options.metadata
      );
      return { status: ZLinkSubmitStatus.Submitted };
    }
    throwIfAborted(options.signal);
    const operation = `MeshNode '${spotRouteTarget.routerChannelId}' send to Spot '${spotRouteTarget.spotId}'`;
    return await this.requireMeshSubmitters().submit(spotRouteTarget.routerChannelId, () => {
      try {
        return mapMeshSubmitResult(node.entrySpot().sendToSpot(
          toBindingRoutingId(spotRouteTarget.targetNodeRid),
          toBindingRoutingId(spotRouteTarget.spotId),
          spotRouteTarget.targetSpotGeneration ?? 0n,
          this.encodeMessage(
            ZLinkChannelMessageKind.Command,
            spotRouteTarget.routerChannelId,
            options.packetName,
            message,
            undefined,
            options.metadata
          ),
          {
            flags: 1,
            routeFence: directSpotRouteFence(spotRouteTarget),
            entrySpot: spotRouteTarget.spotKind === ZLinkSpotKind.Entry
          }
        ));
      } catch (error) {
        throw mapMeshSubmissionError(error, operation);
      }
    }, options.signal);
  }

  async sendFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: {
      readonly packetName?: string;
      readonly signal?: AbortSignal;
      readonly metadata?: ReadonlyMap<string, string>;
    }
  ): Promise<ZLinkSubmitResult> {
    await this.requireManager().routeSendFromSpotToSpot(
      sourceSpot,
      spotRouteTarget,
      options.packetName,
      message,
      options.signal,
      options.metadata
    );
    return { status: ZLinkSubmitStatus.Submitted };
  }

  async requestToSpot<TReply = unknown>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: {
      readonly packetName?: string;
      readonly timeoutMs?: number;
      readonly signal?: AbortSignal;
      readonly metadata?: ReadonlyMap<string, string>;
    }
  ): Promise<TReply> {
    const meshName = spotRouteTarget.routerChannelId;
    const node = this.meshNode(meshName);
    if (node === undefined) {
      return this.requireManager().routeRequestToSpot<TReply>(
        spotRouteTarget,
        options.packetName,
        request,
        options.timeoutMs,
        options.signal,
        options.metadata
      );
    }
    throwIfAborted(options.signal);
    let operationId;
    try {
      operationId = node.entrySpot().requestToSpot(
        toBindingRoutingId(spotRouteTarget.targetNodeRid),
        toBindingRoutingId(spotRouteTarget.spotId),
        spotRouteTarget.targetSpotGeneration ?? 0n,
        this.encodeMessage(
          ZLinkChannelMessageKind.Request,
          meshName,
          options.packetName,
          request,
          options.timeoutMs,
          options.metadata
        ),
        {
          flags: 1,
          timeoutMs: options.timeoutMs,
          routeFence: directSpotRouteFence(spotRouteTarget),
          entrySpot: spotRouteTarget.spotKind === ZLinkSpotKind.Entry
        }
      );
    } catch (error) {
      throw mapMeshSubmissionError(
        error,
        `MeshNode '${meshName}' request to Spot '${spotRouteTarget.spotId}'`
      );
    }
    return this.waitForMeshReply<TReply>(meshName, operationId, options.signal);
  }

  async requestFromSpotToSpot<TReply = unknown>(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply> {
    return this.requireManager().routeRequestFromSpotToSpot<TReply>(
      sourceSpot,
      spotRouteTarget,
      options.packetName,
      request,
      options.timeoutMs,
      options.signal
    );
  }

  async requestRawFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]> {
    return this.requireManager().routeRequestRawFromSpotToSpot(
      sourceSpot,
      spotRouteTarget,
      request,
      options.timeoutMs,
      options.signal
    );
  }

  async requestRawToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]> {
    const meshName = spotRouteTarget.routerChannelId;
    const node = this.meshNode(meshName);
    if (node !== undefined) {
      throwIfAborted(options.signal);
      const payload = JSON.parse(request.getString('utf8')) as Record<string, unknown>;
      const packetName = typeof payload.packetName === 'string'
        ? payload.packetName
        : undefined;
      const parts = this.encodeMessage(
        ZLinkChannelMessageKind.Request,
        meshName,
        packetName,
        payload,
        options.timeoutMs
      );
      let operationId;
      try {
        operationId = node.entrySpot().requestToSpot(
          toBindingRoutingId(spotRouteTarget.targetNodeRid),
          toBindingRoutingId(spotRouteTarget.spotId),
          spotRouteTarget.targetSpotGeneration ?? 0n,
          parts,
          {
            flags: 1,
            timeoutMs: options.timeoutMs,
            routeFence: directSpotRouteFence(spotRouteTarget),
            entrySpot: spotRouteTarget.spotKind === ZLinkSpotKind.Entry
          }
        );
      } catch (error) {
        throw mapMeshSubmissionError(
          error,
          `MeshNode '${meshName}' raw request to Spot '${spotRouteTarget.spotId}'`
        );
      }
      const completion = await this.completionTable(meshName).wait(operationId, options.signal);
      if (completion.terminalResult !== 0 || completion.failureErrno !== 0) {
        try {
          throw meshRequestFailure(meshName, completion.terminalResult, completion.failureErrno);
        } finally {
          closeMeshCompletion(completion);
        }
      }
      try {
        const reply = decodeChannelReply<unknown>(completion.parts, this.codecs);
        return [Message.from(Buffer.from(JSON.stringify(reply)))];
      } finally {
        closeMeshCompletion(completion);
      }
    }
    return this.requireManager().routeRequestRawToSpot(
      spotRouteTarget,
      request,
      options.timeoutMs,
      options.signal
    );
  }

  private requireManager(): ZLinkChannelTransportRuntime {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('Route channel runtime is not started.');
    }
    return manager;
  }

  private requireMeshSubmitters(): ZLinkMeshSubmitterRegistry {
    if (this.meshSubmitters === undefined) {
      throw new ZLinkConfigurationException('MeshNode async admission runtime is not started.');
    }
    return this.meshSubmitters;
  }

  private meshNode(meshName: string): ZLinkBackendMeshNode | undefined {
    const runtime = this.meshRuntime?.();
    return (
      runtime as unknown as {
        meshNode?: (meshName: string) => ZLinkBackendMeshNode | undefined;
      } | undefined
    )?.meshNode?.(meshName);
  }

  private isSelfNode(node: ZLinkBackendMeshNode, targetNodeRid: string): boolean {
    return routingIdsEqual(
      node.status().routingId as unknown as import('../../contracts').RoutingId,
      targetNodeRid
    );
  }

  private isKnownBackendPeer(node: ZLinkBackendMeshNode, targetNodeRid: string): boolean {
    return node.peers().some((peer) => peer.routingId !== null && routingIdsEqual(
      peer.routingId as unknown as import('../../contracts').RoutingId,
      targetNodeRid
    ));
  }

  private submitLocalNode(
    meshName: string,
    sourceNodeRid: string,
    parts: readonly MessageLike[]
  ): ZLinkSubmitResult {
    if (this.localNodeSubmit === undefined) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    return this.localNodeSubmit(meshName, sourceNodeRid, parts);
  }

  private requireMeshNode(meshName: string): ZLinkBackendMeshNode {
    const node = this.meshNode(meshName);
    if (node === undefined) {
      throw new ZLinkConfigurationException(`MeshNode '${meshName}' runtime is not started.`);
    }
    return node;
  }

  private completionTable(meshName: string): ZLinkMeshCompletionTable {
    const table = this.meshRuntime?.()?.meshCompletionTable(meshName);
    if (table === undefined) {
      throw new ZLinkConfigurationException(`MeshNode '${meshName}' completion runtime is not started.`);
    }
    return table;
  }

  private encodeMessage(
    kind: ZLinkChannelMessageKind,
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    timeoutMs?: number,
    metadata?: ReadonlyMap<string, string>
  ): readonly MessageLike[] {
    return encodeChannelEnvelopeParts(
      kind,
      channelName,
      packetName,
      message,
      timeoutMs,
      undefined,
      this.codecs,
      undefined,
      true,
      metadata
    );
  }

  private async waitForMeshReply<TReply>(
    meshName: string,
    operationId: Parameters<ZLinkMeshCompletionTable['wait']>[0],
    signal?: AbortSignal
  ): Promise<TReply> {
    const completion = await this.completionTable(meshName).wait(operationId, signal);
    try {
      if (completion.terminalResult !== 0 || completion.failureErrno !== 0) {
        throw meshRequestFailure(meshName, completion.terminalResult, completion.failureErrno);
      }
      return decodeChannelReply<TReply>(completion.parts, this.codecs);
    } finally {
      closeMeshCompletion(completion);
    }
  }
}

function directSpotRouteFence(target: ZLinkSpotRouteTarget): ServiceDirectSpotRouteFence | undefined {
  // Entry Spots are addressed by their owning MeshNode. They have no
  // Location Store authority row, so only User and Instance Spots carry the
  // exact Ready authority fence required by direct Spot submission.
  if (target.spotKind === ZLinkSpotKind.Entry) {
    return undefined;
  }
  if (
    target.targetSpotGeneration === undefined
    || target.targetNodeGeneration === undefined
    || target.authorityOwnerGeneration === undefined
    || target.targetOwnerId === undefined
    || target.ownerLeaseGeneration === undefined
    || target.authorityStoreVersion === undefined
  ) {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotRouteNotFound,
      `Spot '${String(target.spotId)}' has no complete Ready authority fence.`
    );
  }
  return {
    spot: {
      spotId: String(target.spotId),
      generation: target.targetSpotGeneration
    },
    targetNodeRid: String(target.targetNodeRid),
    targetNodeGeneration: target.targetNodeGeneration,
    authorityOwnerGeneration: target.authorityOwnerGeneration,
    ownerLeaseGeneration: target.ownerLeaseGeneration,
    storeVersion: target.authorityStoreVersion
  };
}

function mapMeshSubmitResult(result: number): ZLinkSubmitResult {
  switch (result) {
    case SubmitResult.Ok:
      return { status: ZLinkSubmitStatus.Submitted };
    case SubmitResult.Backpressured:
    case SubmitResult.NotAdmitted:
      return { status: ZLinkSubmitStatus.Backpressured };
    case SubmitResult.NotFound:
      return { status: ZLinkSubmitStatus.TargetNotFound };
    case SubmitResult.NotConnected:
      return { status: ZLinkSubmitStatus.RouteNotConnected };
    case SubmitResult.Terminated:
      return { status: ZLinkSubmitStatus.Shutdown };
    default:
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        `Mesh submission failed with result ${result}.`
      );
  }
}

function mapMeshSubmissionError(error: unknown, operation: string): Error {
  if (error instanceof ZLinkFrameworkException) {
    return error;
  }
  if (error instanceof SubmitError || error instanceof RequestError) {
    const notFound = error.result === SubmitResult.NotFound || error.result === 102;
    const retriable = error.result === SubmitResult.Backpressured
      || error.result === SubmitResult.NotConnected
      || error.result === 109
      || error.result === 113;
    return new ZLinkFrameworkException(
      notFound
        ? ZLinkFrameworkErrorKind.RequestTargetNotFound
        : ZLinkFrameworkErrorKind.RouteNotConnected,
      `${operation} failed with result ${error.result}.`,
      retriable,
      error
    );
  }
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RouteNotConnected,
    `${operation} failed before native submission completed: ${
      error instanceof Error ? error.message : String(error)
    }`,
    true,
    error
  );
}

function meshRequestFailure(meshName: string, result: number, nativeErrno: number): ZLinkFrameworkException {
  const kind: ZLinkFrameworkErrorKindType = result === 102
    ? ZLinkFrameworkErrorKind.RequestTargetNotFound
    : ZLinkFrameworkErrorKind.RouteNotConnected;
  return new ZLinkFrameworkException(
    kind,
    `MeshNode '${meshName}' request failed with result ${result} and errno ${nativeErrno}.`,
    result === 109 || result === 113
  );
}

function requestMetricOutcome(error: unknown): string {
  if (error instanceof ZLinkFrameworkException) {
    if (error.kind === ZLinkFrameworkErrorKind.RequestTargetNotFound) return 'target_not_found';
    if (error.kind === ZLinkFrameworkErrorKind.DeadlineExceeded) return 'timed_out';
    if (error.kind === ZLinkFrameworkErrorKind.RuntimeShutdown) return 'shutdown';
  }
  if (error instanceof Error && /timed out|deadline/i.test(error.message)) return 'timed_out';
  return 'failed';
}

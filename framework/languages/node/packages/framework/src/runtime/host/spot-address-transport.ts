import {
  ZLinkFrameworkInternalErrorKind,
  createInternalFrameworkException,
  internalFrameworkErrorKind,
  internalFrameworkErrorKindFromWireReply,
  isCanonicalWireReplyTerminal
} from '../framework-errors-internal';
import {
  RequestResult,
  SubmitResult,
  type ZLinkBackendMessageLike as MessageLike
} from '../backend/runtime-values';
import {
  ZLinkFrameworkException,
  type RoutingId
} from '../../contracts';
import {
  ZLinkSubmitStatus,
  type ZLinkSubmitResult
} from '../messaging/submission-result';
import {
  ZLinkSpotKind
} from '../../contracts';
import {
  ZLinkRuntimeMessageFlowOutcome as ZLinkMessageFlowOutcome,
  ZLinkRuntimeDispatchErrorAction as ZLinkDispatchErrorAction,
  ZLinkRuntimeDispatchErrorReason as ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import {
  awaitWithAbort,
  throwIfAborted
} from '../abort';
import type { ZLinkBackendMeshNode } from '../backend/contracts';
import {
  closeMeshCompletion,
  type ZLinkMeshCompletionTable
} from '../backend/mesh-completion-table';
import {
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  type ZLinkChannelEnvelopeCodecRegistry,
  ZLinkChannelMessageKind
} from '../channels/channel-envelope';
import type { ZLinkDispatchErrorReporter } from '../channels';
import { flowIfEnabled } from '../diagnostics';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import type {
  ZLinkSpotAddressCallOptions,
  ZLinkSpotAddressTransport,
  ZLinkSpotRoutedTransport
} from '../spots/spot-outbound';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';

export interface ZLinkHostSpotAddressTransportOptions {
  readonly resolver: () => ZLinkSpotRouteResolver | undefined;
  readonly routed: ZLinkSpotRoutedTransport;
  readonly meshNames: () => readonly string[];
  readonly isMeshConfigured?: (meshName: string) => boolean;
  readonly meshNode: (meshName: string) => ZLinkBackendMeshNode | undefined;
  readonly completions: (meshName: string) => ZLinkMeshCompletionTable | undefined;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly defaultRequestTimeoutMs: number;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
}

export function hasObjectClientCapability(
  role: 'none' | 'client' | 'server' | undefined
): boolean {
  return role === 'client' || role === 'server';
}

/** Owns global Spot authority lookup and Missing Instance placement. */
export class ZLinkHostSpotAddressTransport implements ZLinkSpotAddressTransport {
  constructor(private readonly options: ZLinkHostSpotAddressTransportOptions) {}

  async sendToSpotAddress(
    spotId: RoutingId,
    message: unknown,
    call: ZLinkSpotAddressCallOptions
  ): Promise<ZLinkSubmitResult> {
    const existing = await this.resolveExisting(spotId, call.signal, call.instanceSpot);
    if (existing !== undefined) {
      this.validateExisting(existing, call);
      try {
        const result = await this.options.routed.sendToSpot(existing, message, {
          signal: call.signal,
          metadata: call.metadata
        });
        if (result.status === ZLinkSubmitStatus.Submitted) {
          this.traceInstanceAddress(
            ZLinkMessageFlowOutcome.Sent,
            ZLinkDispatchMessageKind.Send,
            spotId,
            message,
            existing.routerChannelId,
            existing.stableType,
            existing.targetNodeRid
          );
        } else {
          this.traceInstanceAddress(
            ZLinkMessageFlowOutcome.Dropped,
            ZLinkDispatchMessageKind.Send,
            spotId,
            message,
            existing.routerChannelId,
            existing.stableType,
            existing.targetNodeRid,
            submitResultReason(result.status)
          );
        }
        if (
          result.status === ZLinkSubmitStatus.TargetNotFound
          || result.status === ZLinkSubmitStatus.RouteNotConnected
        ) {
          this.options.resolver()?.invalidate?.(spotId);
        }
        return result;
      } catch (error) {
        if (isStaleSpotRouteError(error)) {
          this.options.resolver()?.invalidate?.(spotId);
        }
        this.traceInstanceAddress(
          ZLinkMessageFlowOutcome.Dropped,
          ZLinkDispatchMessageKind.Send,
          spotId,
          message,
          existing.routerChannelId,
          existing.stableType,
          existing.targetNodeRid,
          addressedInstanceErrorReason(error)
        );
        throw error;
      }
    }
    if (!call.instanceSpot) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    const timeoutMs = call.timeoutMs ?? this.options.defaultRequestTimeoutMs;
    const deadlineMs = Date.now() + Math.max(0, timeoutMs);
    const selected = await this.waitForMissingTarget(spotId, call, deadlineMs);
    if (selected === undefined) {
      this.traceInstanceAddress(
        ZLinkMessageFlowOutcome.Dropped,
        ZLinkDispatchMessageKind.Send,
        spotId,
        message,
        call.initialMeshName,
        call.instanceSpotType,
        undefined,
        ZLinkDispatchErrorReason.StaleTarget
      );
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    const remainingMs = Math.max(1, deadlineMs - Date.now());
    const result = selected.node.sendToMissingInstanceSpot(
      selected.target,
      this.encode(ZLinkChannelMessageKind.Command, selected.meshName, message),
      BigInt(Date.now() + remainingMs),
      call.sourceSpot === undefined ? undefined : String(call.sourceSpot.routingId),
      call.metadata
    );
    const mapped = mapSubmitResult(result);
    this.traceInstanceAddress(
      mapped.status === ZLinkSubmitStatus.Submitted
        ? ZLinkMessageFlowOutcome.Sent
        : ZLinkMessageFlowOutcome.Dropped,
      ZLinkDispatchMessageKind.Send,
      spotId,
      message,
      selected.meshName,
      selected.target.stableType,
      selected.target.targetNodeRid,
      mapped.status === ZLinkSubmitStatus.Submitted ? undefined : submitResultReason(mapped.status)
    );
    return mapped;
  }

  async requestToSpotAddress<TReply = unknown>(
    spotId: RoutingId,
    request: unknown,
    call: ZLinkSpotAddressCallOptions
  ): Promise<TReply> {
    return this.requestToSpotAddressOnce<TReply>(spotId, request, call);
  }

  private async requestToSpotAddressOnce<TReply>(
    spotId: RoutingId,
    request: unknown,
    call: ZLinkSpotAddressCallOptions
  ): Promise<TReply> {
    const existing = await this.resolveExisting(spotId, call.signal, call.instanceSpot);
    if (existing !== undefined) {
      this.validateExisting(existing, call);
      try {
        this.traceInstanceAddress(
          ZLinkMessageFlowOutcome.Sent,
          ZLinkDispatchMessageKind.Request,
          spotId,
          request,
          existing.routerChannelId,
          existing.stableType,
          existing.targetNodeRid
        );
        const reply = await this.options.routed.requestToSpot<TReply>(existing, request, {
          timeoutMs: call.timeoutMs,
          signal: call.signal,
          metadata: call.metadata
        });
        this.traceInstanceAddress(
          ZLinkMessageFlowOutcome.ReplyReceived,
          ZLinkDispatchMessageKind.Request,
          spotId,
          request,
          existing.routerChannelId,
          existing.stableType,
          existing.targetNodeRid
        );
        return reply;
      } catch (error) {
        if (
          error instanceof ZLinkFrameworkException
          && (
            internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.SpotRouteNotFound
            || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.SpotGenerationStale
            || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.SpotMoving
            || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.RequestTargetNotFound
            || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.RouteNotConnected
          )
        ) {
          this.options.resolver()?.invalidate?.(spotId);
        }
        this.options.dispatchErrors?.report({
          surface: ZLinkDispatchErrorSurface.InstanceSpot,
          messageKind: ZLinkDispatchMessageKind.Request,
          packetName: resolveFrameworkPacketName(request, undefined, 'Channel'),
          meshName: existing.routerChannelId,
          targetRid: existing.targetNodeRid,
          spotId: String(spotId),
          instanceSpotType: existing.stableType,
          reason: addressedInstanceErrorReason(error),
          action: ZLinkDispatchErrorAction.FailCaller,
          error
        });
        throw error;
      }
    }
    if (!call.instanceSpot) {
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.RequestTargetNotFound,
        `Spot '${String(spotId)}' has no Ready authority.`
      );
    }
    const timeoutMs = call.timeoutMs ?? this.options.defaultRequestTimeoutMs;
    const deadlineMs = Date.now() + Math.max(0, timeoutMs);
    const selected = await this.waitForMissingTarget(spotId, call, deadlineMs);
    if (selected === undefined) {
      const error = createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.RequestTargetNotFound,
        `No eligible Instance Spot target serves '${String(spotId)}'.`
      );
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.InstanceSpot,
        messageKind: ZLinkDispatchMessageKind.Request,
        packetName: resolveFrameworkPacketName(request, undefined, 'Channel'),
        meshName: call.initialMeshName,
        spotId: String(spotId),
        instanceSpotType: call.instanceSpotType,
        reason: ZLinkDispatchErrorReason.StaleTarget,
        action: ZLinkDispatchErrorAction.FailCaller,
        error
      });
      throw error;
    }
    const remainingMs = Math.max(1, deadlineMs - Date.now());
    const operation = selected.node.requestToMissingInstanceSpot(
      selected.target,
      this.encode(ZLinkChannelMessageKind.Request, selected.meshName, request, remainingMs),
      remainingMs,
      call.sourceSpot === undefined ? undefined : String(call.sourceSpot.routingId),
      call.metadata
    );
    this.traceInstanceAddress(
      ZLinkMessageFlowOutcome.Sent,
      ZLinkDispatchMessageKind.Request,
      spotId,
      request,
      selected.meshName,
      selected.target.stableType,
      selected.target.targetNodeRid
    );
    const table = this.options.completions(selected.meshName);
    if (table === undefined) {
      throw new Error(`MeshNode '${selected.meshName}' completion table is not started.`);
    }
    const completion = await table.wait(operation, call.signal);
    try {
      if (completion.terminalResult !== 0 || completion.failureErrno !== 0) {
        const error = missingInstanceRequestFailure(
          completion.terminalResult,
          completion.failureErrno
        );
        this.options.dispatchErrors?.report({
          surface: ZLinkDispatchErrorSurface.InstanceSpot,
          messageKind: ZLinkDispatchMessageKind.Request,
          packetName: resolveFrameworkPacketName(request, undefined, 'Channel'),
          meshName: selected.meshName,
          targetRid: selected.target.targetNodeRid,
          spotId: String(spotId),
          instanceSpotType: selected.target.stableType,
          reason: addressedInstanceErrorReason(error),
          action: ZLinkDispatchErrorAction.FailCaller,
          error
        });
        throw error;
      }
      const reply = decodeChannelReply<TReply>(completion.parts, this.options.codecs);
      this.traceInstanceAddress(
        ZLinkMessageFlowOutcome.ReplyReceived,
        ZLinkDispatchMessageKind.Request,
        spotId,
        request,
        selected.meshName,
        selected.target.stableType,
        selected.target.targetNodeRid
      );
      return reply;
    } finally {
      closeMeshCompletion(completion);
    }
  }

  private async waitForMissingTarget(
    spotId: RoutingId,
    call: ZLinkSpotAddressCallOptions,
    deadlineMs: number
  ): Promise<ReturnType<ZLinkHostSpotAddressTransport['selectMissingTarget']>> {
    throwIfAborted(call.signal);
    let selected = this.selectMissingTarget(spotId, call);
    while (selected === undefined && Date.now() < deadlineMs) {
      throwIfAborted(call.signal);
      const remainingMs = deadlineMs - Date.now();
      await awaitWithAbort(
        new Promise<void>(resolve => setTimeout(resolve, Math.min(10, remainingMs))),
        call.signal
      );
      selected = this.selectMissingTarget(spotId, call);
    }
    return selected;
  }

  private traceInstanceAddress(
    outcome: ZLinkMessageFlowOutcome,
    messageKind: ZLinkDispatchMessageKind,
    spotId: RoutingId,
    message: unknown,
    meshName: string | undefined,
    instanceSpotType: string | undefined,
    targetRid: string | undefined,
    errorReason?: ZLinkDispatchErrorReason
  ): void {
    const flow = flowIfEnabled(this.options.dispatchErrors?.flow, outcome);
    if (flow === undefined) return;
    flow.trace({
      outcome,
      surface: ZLinkDispatchErrorSurface.InstanceSpot,
      messageKind,
      packetName: resolveFrameworkPacketName(message, undefined, 'Channel'),
      meshName,
      targetRid,
      spotId: String(spotId),
      instanceSpotType,
      errorReason
    });
  }

  private async resolveExisting(
    spotId: RoutingId,
    signal?: AbortSignal,
    refreshInstanceRoute = false
  ): Promise<import('../spots/spot-routing-internal').ZLinkSpotRouteTarget | undefined> {
    const resolver = this.options.resolver();
    if (resolver === undefined) {
      throw new Error('Global Spot address resolution requires a Location Store.');
    }
    // An Instance intent may create a new incarnation after an explicit close.
    // Do not let a positive cache entry for the previous incarnation turn that
    // first message into a stale-target failure. The operation still performs
    // one resolve and one submit; this is a fresh lookup, not a hidden retry.
    if (refreshInstanceRoute) resolver.invalidate?.(spotId);
    try {
      return await resolver.resolve(spotId, signal);
    } catch (error) {
      if (
        error instanceof ZLinkFrameworkException
        && internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.SpotRouteNotFound
      ) {
        return undefined;
      }
      throw error;
    }
  }

  private selectMissingTarget(
    spotId: RoutingId,
    call: ZLinkSpotAddressCallOptions
  ): {
    readonly meshName: string;
    readonly node: ZLinkBackendMeshNode;
    readonly target: {
      readonly targetNodeRid: string;
      readonly targetNodeGeneration: bigint;
      readonly targetSpotId: string;
      readonly stableType: string;
      readonly descriptorVersion: string;
    };
  } | undefined {
    const configuredMeshes = this.options.meshNames();
    if (
      call.initialMeshName !== undefined
      && this.options.isMeshConfigured?.(call.initialMeshName) === false
    ) {
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.MeshNotFound,
        `RouteMesh '${call.initialMeshName}' is not configured.`
      );
    }
    if (call.initialMeshName !== undefined && !configuredMeshes.includes(call.initialMeshName)) {
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.ObjectClientNotConfigured,
        `RouteMesh '${call.initialMeshName}' has no object-client role.`
      );
    }
    if (call.initialMeshName === undefined && configuredMeshes.length === 0) {
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.ObjectClientNotConfigured,
        'No object-client RouteMesh is configured.'
      );
    }
    if (call.initialMeshName === undefined && configuredMeshes.length > 1) {
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.MeshSelectionRequired,
        'Multiple object-client RouteMeshes are configured; call inMesh(...).'
      );
    }
    const meshNames = call.initialMeshName === undefined
      ? configuredMeshes
      : [call.initialMeshName];
    const distinctTypes = [...new Set(meshNames.flatMap(meshName =>
      this.options.meshNode(meshName)?.instanceSpotPlacementTypes?.() ?? []
    ))];
    const stableType = call.instanceSpotType
      ?? (distinctTypes.length === 1 ? distinctTypes[0] : undefined);
    if (stableType === undefined) {
      if (distinctTypes.length === 0) return undefined;
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.InvalidConfiguration,
        'Instance Spot type is required when multiple types are registered.'
      );
    }
    for (const meshName of meshNames) {
      const node = this.options.meshNode(meshName);
      const placement = node?.selectObjectPlacement(stableType);
      if (node !== undefined && placement !== undefined) {
        return {
          meshName,
          node,
          target: {
            ...placement,
            targetSpotId: String(spotId),
            stableType
          }
        };
      }
    }
    return undefined;
  }

  private validateExisting(
    target: import('../spots/spot-routing-internal').ZLinkSpotRouteTarget,
    call: ZLinkSpotAddressCallOptions
  ): void {
    if (!call.instanceSpot) return;
    if (
      target.spotKind !== ZLinkSpotKind.Instance
      || (
        call.instanceSpotType !== undefined
        && target.stableType !== call.instanceSpotType
      )
    ) {
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.SpotTypeMismatch,
        `Spot '${String(target.spotId)}' is not the requested Instance Spot type.`
      );
    }
  }

  private encode(
    kind: ZLinkChannelMessageKind,
    meshName: string,
    payload: unknown,
    timeoutMs?: number
  ): readonly MessageLike[] {
    return encodeChannelEnvelopeParts(
      kind,
      meshName,
      undefined,
      payload,
      timeoutMs,
      undefined,
      this.options.codecs,
      undefined,
      true,
      new Map()
    );
  }
}

function missingInstanceRequestFailure(
  result: number,
  nativeErrno: number
): ZLinkFrameworkException {
  const canonical = isCanonicalWireReplyTerminal(result, nativeErrno);
  const wireKind = canonical
    ? internalFrameworkErrorKindFromWireReply(result, nativeErrno)
    : undefined;
  const kind = !canonical
    ? ZLinkFrameworkInternalErrorKind.RequestProtocolError
    : result === RequestResult.NotFound
      ? ZLinkFrameworkInternalErrorKind.RequestTargetNotFound
      : result === RequestResult.TimedOut
        ? ZLinkFrameworkInternalErrorKind.DeadlineExceeded
        : result === RequestResult.Terminated
          ? ZLinkFrameworkInternalErrorKind.RuntimeShutdown
          : result === RequestResult.NotConnected || result === RequestResult.Backpressured
            ? ZLinkFrameworkInternalErrorKind.RouteNotConnected
            : wireKind ?? ZLinkFrameworkInternalErrorKind.RequestFailed;
  return createInternalFrameworkException(
    kind,
    `Instance Spot request failed with result ${result} and errno ${nativeErrno}.`
  );
}

function mapSubmitResult(result: number): ZLinkSubmitResult {
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
      throw createInternalFrameworkException(
        ZLinkFrameworkInternalErrorKind.RequestFailed,
        `Instance Spot submission failed with result ${result}.`
      );
  }
}

function isStaleSpotRouteError(error: unknown): boolean {
  return error instanceof ZLinkFrameworkException
    && (
      internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.SpotRouteNotFound
      || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.SpotGenerationStale
      || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.SpotMoving
      || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.RequestTargetNotFound
      || internalFrameworkErrorKind(error) === ZLinkFrameworkInternalErrorKind.RouteNotConnected
    );
}

function submitResultReason(
  status: ZLinkSubmitStatus
): ZLinkDispatchErrorReason {
  switch (status) {
    case ZLinkSubmitStatus.Backpressured:
    case ZLinkSubmitStatus.TimedOut:
      return ZLinkDispatchErrorReason.Backpressure;
    case ZLinkSubmitStatus.Shutdown:
      return ZLinkDispatchErrorReason.Shutdown;
    case ZLinkSubmitStatus.TargetNotFound:
    case ZLinkSubmitStatus.RouteNotConnected:
      return ZLinkDispatchErrorReason.StaleTarget;
    case ZLinkSubmitStatus.Submitted:
      return ZLinkDispatchErrorReason.HandlerException;
  }
}

function addressedInstanceErrorReason(error: unknown): ZLinkDispatchErrorReason {
  if (error instanceof ZLinkFrameworkException) {
    const kind = internalFrameworkErrorKind(error);
    if (
      kind === ZLinkFrameworkInternalErrorKind.SpotRouteNotFound
      || kind === ZLinkFrameworkInternalErrorKind.SpotGenerationStale
      || kind === ZLinkFrameworkInternalErrorKind.SpotMoving
      || kind === ZLinkFrameworkInternalErrorKind.RequestTargetNotFound
      || kind === ZLinkFrameworkInternalErrorKind.RouteNotConnected
    ) {
      return ZLinkDispatchErrorReason.StaleTarget;
    }
    if (kind === ZLinkFrameworkInternalErrorKind.RuntimeShutdown) {
      return ZLinkDispatchErrorReason.Shutdown;
    }
  }
  return ZLinkDispatchErrorReason.HandlerException;
}

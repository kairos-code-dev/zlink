import {
  SubmitResult,
  type MessageLike
} from '@zlink-systems/zlink';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSubmitStatus,
  type RoutingId,
  type ZLinkSubmitResult
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
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
  readonly instanceTypes: (meshName: string) => readonly string[];
  readonly meshNode: (meshName: string) => ZLinkBackendMeshNode | undefined;
  readonly completions: (meshName: string) => ZLinkMeshCompletionTable | undefined;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly defaultRequestTimeoutMs: number;
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
    const existing = await this.resolveExisting(spotId, call.signal);
    if (existing !== undefined) {
      this.validateExisting(existing, call);
      return await this.options.routed.sendToSpot(existing, message, {
        signal: call.signal,
        metadata: call.metadata
      });
    }
    if (!call.instanceSpot) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    const selected = this.selectMissingTarget(spotId, call);
    if (selected === undefined) {
      return { status: ZLinkSubmitStatus.TargetNotFound };
    }
    const timeoutMs = this.options.defaultRequestTimeoutMs;
    const result = selected.node.sendToMissingInstanceSpot(
      selected.target,
      this.encode(ZLinkChannelMessageKind.Command, selected.meshName, message),
      BigInt(Date.now() + timeoutMs),
      call.sourceSpot === undefined ? undefined : String(call.sourceSpot.routingId),
      call.metadata
    );
    return mapSubmitResult(result);
  }

  async requestToSpotAddress<TReply = unknown>(
    spotId: RoutingId,
    request: unknown,
    call: ZLinkSpotAddressCallOptions
  ): Promise<TReply> {
    const existing = await this.resolveExisting(spotId, call.signal);
    if (existing !== undefined) {
      this.validateExisting(existing, call);
      return await this.options.routed.requestToSpot<TReply>(existing, request, {
        timeoutMs: call.timeoutMs,
        signal: call.signal,
        metadata: call.metadata
      });
    }
    if (!call.instanceSpot) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestTargetNotFound,
        `Spot '${String(spotId)}' has no Ready authority.`
      );
    }
    const selected = this.selectMissingTarget(spotId, call);
    if (selected === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestTargetNotFound,
        `No eligible Instance Spot target serves '${String(spotId)}'.`
      );
    }
    const timeoutMs = call.timeoutMs ?? this.options.defaultRequestTimeoutMs;
    const operation = selected.node.requestToMissingInstanceSpot(
      selected.target,
      this.encode(ZLinkChannelMessageKind.Request, selected.meshName, request, timeoutMs),
      timeoutMs,
      call.sourceSpot === undefined ? undefined : String(call.sourceSpot.routingId),
      call.metadata
    );
    const table = this.options.completions(selected.meshName);
    if (table === undefined) {
      throw new Error(`MeshNode '${selected.meshName}' completion table is not started.`);
    }
    const completion = await table.wait(operation, call.signal);
    try {
      if (completion.terminalResult !== 0 || completion.failureErrno !== 0) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.RequestFailed,
          `Instance Spot request failed with result ${completion.terminalResult} `
          + `and errno ${completion.failureErrno}.`
        );
      }
      return decodeChannelReply<TReply>(completion.parts, this.options.codecs);
    } finally {
      closeMeshCompletion(completion);
    }
  }

  private async resolveExisting(
    spotId: RoutingId,
    signal?: AbortSignal
  ): Promise<import('../spots/spot-routing-internal').ZLinkSpotRouteTarget | undefined> {
    const resolver = this.options.resolver();
    if (resolver === undefined) {
      throw new Error('Global Spot address resolution requires a Location Store.');
    }
    try {
      return await resolver.resolve(spotId, signal);
    } catch (error) {
      if (
        error instanceof ZLinkFrameworkException
        && error.kind === ZLinkFrameworkErrorKind.SpotRouteNotFound
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
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.MeshNotFound,
        `RouteMesh '${call.initialMeshName}' is not configured.`
      );
    }
    if (call.initialMeshName !== undefined && !configuredMeshes.includes(call.initialMeshName)) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ObjectClientNotConfigured,
        `RouteMesh '${call.initialMeshName}' has no object-client role.`
      );
    }
    if (call.initialMeshName === undefined && configuredMeshes.length === 0) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ObjectClientNotConfigured,
        'No object-client RouteMesh is configured.'
      );
    }
    if (call.initialMeshName === undefined && configuredMeshes.length > 1) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.MeshSelectionRequired,
        'Multiple object-client RouteMeshes are configured; call inMesh(...).'
      );
    }
    const meshNames = call.initialMeshName === undefined
      ? configuredMeshes
      : [call.initialMeshName];
    const distinctTypes = [...new Set(
      meshNames.flatMap(meshName => this.options.instanceTypes(meshName))
    )];
    const stableType = call.instanceSpotType
      ?? (distinctTypes.length === 1 ? distinctTypes[0] : undefined);
    if (stableType === undefined) {
      if (distinctTypes.length === 0) return undefined;
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.InvalidConfiguration,
        'Instance Spot type is required when multiple types are registered.'
      );
    }
    for (const meshName of meshNames) {
      if (!this.options.instanceTypes(meshName).includes(stableType)) continue;
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
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotTypeMismatch,
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
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        `Instance Spot submission failed with result ${result}.`
      );
  }
}

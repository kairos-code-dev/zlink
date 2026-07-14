import type {
  RoutingId,
  SpotHandle,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkPublishCall,
  ZLinkRequestCall,
  ZLinkSendCall,
  ZLinkSpotOutbound,
  ZLinkSpotPublisherClient
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import { ZLinkConfigurationException } from '../configuration';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts/Errors/ZLinkFrameworkException';
import type { ZLinkBackendSpot } from '../backend/contracts';
import { deliverOnSerial } from '../workers';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import type { ZLinkSpotRouteTarget } from './spot-routing-internal';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import {
  refreshSpotHandle,
  resolveSpotHandle,
  type ResolvedSpotHandle
} from './spot-handle';

export class DefaultZLinkSpotOutbound implements ZLinkSpotOutbound {
  constructor(
    private readonly serial: ZLinkSpotSerialExecutor,
    private readonly channelClient?: ZLinkChannelClient,
    _fanoutClient?: ZLinkFanoutClient,
    private readonly spotPublisherClient?: ZLinkSpotPublisherClient,
    private readonly routedTransport?: ZLinkSpotRoutedTransport,
    private readonly spotRouterChannelIdForMesh: (meshName: string) => string = (meshName) => meshName,
    private readonly sourceSpotProvider?: () => ZLinkBackendSpot | undefined
  ) {}

  sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall {
    return wrapRoutedSpotSendCall(
      this.serial,
      this.requireRoutedTransport(),
      spot,
      message,
      this.spotRouterChannelIdForMesh,
      this.sourceSpotProvider
    );
  }

  requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall {
    return wrapRoutedSpotRequestCall(
      this.serial,
      this.requireRoutedTransport(),
      spot,
      request,
      this.spotRouterChannelIdForMesh,
      this.sourceSpotProvider
    );
  }

  publish(topic: string, event: unknown): ZLinkPublishCall {
    if (this.spotPublisherClient !== undefined) {
      return wrapPublishCall(this.serial, this.spotPublisherClient.publish('', topic, event));
    }
    throw new ZLinkConfigurationException('Spot outbound publish requires a configured Spot publisher client.');
  }

  sendToChannel(channelName: string, message: unknown): ZLinkSendCall {
    return wrapSendCall(this.serial, this.requireChannelClient().sendToChannel(channelName, message));
  }

  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall {
    return wrapRequestCall(this.serial, this.requireChannelClient().requestToChannel(channelName, request));
  }

  private requireChannelClient(): ZLinkChannelClient {
    if (this.channelClient === undefined) {
      throw new ZLinkConfigurationException('Spot channel outbound runtime is not started.');
    }
    return this.channelClient;
  }

  private requireRoutedTransport(): ZLinkSpotRoutedTransport {
    if (this.routedTransport === undefined) {
      throw new ZLinkConfigurationException('Spot routed outbound runtime is not started.');
    }
    return this.routedTransport;
  }
}

export interface ZLinkSpotRoutedTransport {
  sendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: ZLinkSpotRoutedSendOptions
  ): Promise<void>;
  sendFromSpotToSpot?(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: ZLinkSpotRoutedSendOptions
  ): Promise<void>;
  requestToSpot<TReply = unknown>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: ZLinkSpotRoutedRequestOptions
  ): Promise<TReply>;
  requestFromSpotToSpot?<TReply = unknown>(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: ZLinkSpotRoutedRequestOptions
  ): Promise<TReply>;
}

export interface ZLinkSpotRoutedSendOptions {
  readonly packetName?: string;
  readonly signal?: AbortSignal;
}

export interface ZLinkSpotRoutedRequestOptions extends ZLinkSpotRoutedSendOptions {
  readonly timeoutMs?: number;
}

function wrapSendCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkSendCall): ZLinkSendCall {
  return wrapFireAndForgetPacketCall(serial, inner);
}

function wrapPublishCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkPublishCall): ZLinkPublishCall {
  return wrapFireAndForgetPacketCall(serial, inner);
}

function wrapFireAndForgetPacketCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkSendCall): ZLinkSendCall;
function wrapFireAndForgetPacketCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkPublishCall): ZLinkPublishCall;
function wrapFireAndForgetPacketCall(
  serial: ZLinkSpotSerialExecutor,
  inner: ZLinkSendCall | ZLinkPublishCall
): ZLinkSendCall {
  return {
    submit() {
      void serial.execute(() => inner.submit()).catch(() => undefined);
    }
  };
}

function wrapRequestCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkRequestCall): ZLinkRequestCall {
  return {
    timeout(timeoutMs: number) {
      inner.timeout(timeoutMs);
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      const insideCurrentTurn = serial.isCurrentTurn;
      const pending = startRequestOnSerial(serial, () => ({ pending: inner.submit<TReply>(signal) }));
      return insideCurrentTurn ? serial.yieldPromise(pending) : deliverOnSerial(serial, pending);
    }
  };
}

/**
 * Starts an outbound request in Spot serial order without gating the serial
 * line on the request round trip. When already running inside the Spot line
 * (a handler turn), the request starts immediately as part of that turn;
 * otherwise the start is enqueued as its own serial turn.
 */
function startRequestOnSerial<TReply>(
  serial: ZLinkSpotSerialExecutor,
  begin: () => Promise<{ pending: Promise<TReply> }> | { pending: Promise<TReply> }
): Promise<TReply> {
  return serial.execute(begin).then((startedRequest) => startedRequest.pending);
}

function wrapRoutedSpotSendCall(
  serial: ZLinkSpotSerialExecutor,
  transport: ZLinkSpotRoutedTransport,
  spot: SpotHandle,
  message: unknown,
  spotRouterChannelIdForMesh: (meshName: string) => string,
  sourceSpotProvider?: () => ZLinkBackendSpot | undefined
): ZLinkSendCall {
  return {
    submit() {
      void serial.execute(async () => {
        await sendToSpotHandle(
          transport,
          spot,
          message,
          { spotRouterChannelIdForMesh, sourceSpot: sourceSpotProvider?.() }
        );
      }).catch(() => undefined);
    }
  };
}

function wrapRoutedSpotRequestCall(
  serial: ZLinkSpotSerialExecutor,
  transport: ZLinkSpotRoutedTransport,
  spot: SpotHandle,
  request: unknown,
  spotRouterChannelIdForMesh: (meshName: string) => string,
  sourceSpotProvider?: () => ZLinkBackendSpot | undefined
): ZLinkRequestCall {
  let selectedTimeoutMs: number | undefined;
  const begin = <TReply>(signal?: AbortSignal) => startRequestOnSerial<TReply>(serial, () => ({
    pending: requestToSpotHandle<TReply>(transport, spot, request, {
      timeoutMs: selectedTimeoutMs,
      signal,
      spotRouterChannelIdForMesh,
      sourceSpot: sourceSpotProvider?.()
    })
  }));
  return {
    timeout(timeoutMs: number) {
      selectedTimeoutMs = timeoutMs;
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      const insideCurrentTurn = serial.isCurrentTurn;
      const pending = begin<TReply>(signal);
      return insideCurrentTurn ? serial.yieldPromise(pending) : deliverOnSerial(serial, pending);
    }
  };
}

export interface ZLinkSpotHandleCallOptions {
  readonly timeoutMs?: number;
  readonly signal?: AbortSignal;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly sourceSpot?: ZLinkBackendSpot;
}

export async function sendToSpotHandle(
  transport: ZLinkSpotRoutedTransport,
  spot: SpotHandle,
  message: unknown,
  options: ZLinkSpotHandleCallOptions = {}
): Promise<void> {
  const packetName = resolveFrameworkPacketName(message, undefined, 'SPOT');
  const target = spotRefToSpotRouteTarget(
    await requireSpotRef(spot, options.signal),
    options.spotRouterChannelIdForMesh
  );
  if (options.sourceSpot !== undefined && transport.sendFromSpotToSpot !== undefined) {
    await transport.sendFromSpotToSpot(options.sourceSpot, target, message, {
      packetName,
      signal: options.signal
    });
    return;
  }
  await transport.sendToSpot(target, message, { packetName, signal: options.signal });
}

export async function requestToSpotHandle<TReply = unknown>(
  transport: ZLinkSpotRoutedTransport,
  spot: SpotHandle,
  request: unknown,
  options: ZLinkSpotHandleCallOptions = {}
): Promise<TReply> {
  const packetName = resolveFrameworkPacketName(request, undefined, 'SPOT');
  const requestResolved = async (resolved: ResolvedSpotHandle): Promise<TReply> => {
    const target = spotRefToSpotRouteTarget(resolved, options.spotRouterChannelIdForMesh);
    const transportOptions = {
      packetName,
      timeoutMs: options.timeoutMs,
      signal: options.signal
    };
    if (options.sourceSpot !== undefined && transport.requestFromSpotToSpot !== undefined) {
      return await transport.requestFromSpotToSpot<TReply>(
        options.sourceSpot,
        target,
        request,
        transportOptions
      );
    }
    return await transport.requestToSpot<TReply>(target, request, transportOptions);
  };

  try {
    return await requestResolved(await requireSpotRef(spot, options.signal));
  } catch (error) {
    if (!isSafeStaleSpotFailure(error)) {
      throw error;
    }
    const refreshed = await refreshSpotHandle(spot, options.signal);
    if (refreshed === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotRouteNotFound,
        `Spot '${spot.spotRid}' has no live location after refresh.`,
        false,
        error
      );
    }
    return await requestResolved(refreshed);
  }
}

function isSafeStaleSpotFailure(error: unknown): boolean {
  return error instanceof ZLinkFrameworkException && (
    error.kind === ZLinkFrameworkErrorKind.RequestTargetNotFound
    || error.kind === ZLinkFrameworkErrorKind.SpotRouteNotFound
  );
}

async function requireSpotRef(handle: SpotHandle, signal?: AbortSignal): Promise<ResolvedSpotHandle> {
  const resolved = await resolveSpotHandle(handle, signal);
  if (resolved === undefined) {
    throw new ZLinkConfigurationException(`Spot '${handle.spotRid}' has no live location.`);
  }
  return resolved;
}

function spotRefToSpotRouteTarget(
  spot: ResolvedSpotHandle,
  spotRouterChannelIdForMesh: (meshName: string) => string = (meshName) => meshName
): ZLinkSpotRouteTarget {
  return {
    routerChannelId: spotRouterChannelIdForMesh(spot.meshName),
    targetNodeRid: normalizeSpotRefRoutingId(spot.nodeRid),
    spotRid: normalizeSpotRefRoutingId(spot.spotRid),
    spotKind: spot.spotKind ?? ZLinkSpotKind.User
  };
}

function normalizeSpotRefRoutingId(routingId: RoutingId): RoutingId {
  const value = routingId as unknown;
  if (value instanceof BindingRoutingId) {
    return routingId;
  }
  if (typeof value === 'string') {
    return BindingRoutingId.from(value) as unknown as RoutingId;
  }
  const bytes = routingIdBytesOf(value);
  if (bytes !== undefined) {
    return BindingRoutingId.from(bytes) as unknown as RoutingId;
  }
  const toHex = (value as { readonly toHex?: unknown } | null)?.toHex;
  if (typeof toHex === 'function') {
    return BindingRoutingId.fromHex(toHex.call(value)) as unknown as RoutingId;
  }
  return BindingRoutingId.from(String(routingId)) as unknown as RoutingId;
}

function routingIdBytesOf(value: unknown): Uint8Array | undefined {
  if (value === null || typeof value !== 'object') {
    return undefined;
  }
  const toBytes = (value as { readonly toBytes?: unknown }).toBytes;
  if (typeof toBytes === 'function') {
    const bytes = toBytes.call(value);
    return bytes instanceof Uint8Array ? bytes : undefined;
  }
  const candidate = (value as { readonly bytes?: unknown; readonly _bytes?: unknown }).bytes
    ?? (value as { readonly _bytes?: unknown })._bytes;
  if (candidate instanceof Uint8Array) {
    return candidate;
  }
  const data = (candidate as { readonly data?: unknown } | undefined)?.data;
  if (Array.isArray(data) && data.every((item) => Number.isInteger(item) && item >= 0 && item <= 255)) {
    return Uint8Array.from(data);
  }
  return undefined;
}

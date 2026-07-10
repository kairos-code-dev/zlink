import type {
  RoutingId,
  SpotRef,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkPublishCall,
  ZLinkRequestCall,
  ZLinkSendCall,
  ZLinkYieldRequestCall,
  ZLinkSpotOutbound,
  ZLinkSpotPublisherClient
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkBackendSpot } from '../backend/contracts';
import { deliverOnSerial } from '../workers';
import type { ZLinkSpotRouteTarget } from './spot-routing-internal';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';

export class DefaultZLinkSpotOutbound implements ZLinkSpotOutbound {
  constructor(
    private readonly serial: ZLinkSpotSerialExecutor,
    private readonly channelClient?: ZLinkChannelClient,
    private readonly fanoutClient?: ZLinkFanoutClient,
    private readonly spotPublisherClient?: ZLinkSpotPublisherClient,
    private readonly routedTransport?: ZLinkSpotRoutedTransport,
    private readonly spotRouterChannelIdForMesh: (meshName: string) => string = (meshName) => meshName,
    private readonly sourceSpotProvider?: () => ZLinkBackendSpot | undefined
  ) {}

  sendToSpot(spot: SpotRef, message: unknown): ZLinkSendCall {
    return wrapRoutedSpotSendCall(
      this.serial,
      this.requireRoutedTransport(),
      spot,
      message,
      this.spotRouterChannelIdForMesh,
      this.sourceSpotProvider
    );
  }

  requestToSpot(spot: SpotRef, request: unknown): ZLinkYieldRequestCall {
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
    return wrapPublishCall(this.serial, this.requireFanoutClient().publish(topic, event));
  }

  sendToChannel(channelName: string, message: unknown): ZLinkSendCall {
    return wrapSendCall(this.serial, this.requireChannelClient().sendToChannel(channelName, message));
  }

  requestToChannel(channelName: string, request: unknown): ZLinkYieldRequestCall {
    return wrapRequestCall(this.serial, this.requireChannelClient().requestToChannel(channelName, request));
  }

  private requireChannelClient(): ZLinkChannelClient {
    if (this.channelClient === undefined) {
      throw new ZLinkConfigurationException('Spot channel outbound runtime is not started.');
    }
    return this.channelClient;
  }

  private requireFanoutClient(): ZLinkFanoutClient {
    if (this.fanoutClient === undefined) {
      throw new ZLinkConfigurationException('Spot publisher runtime is not started.');
    }
    return this.fanoutClient;
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
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    submit(signal?: AbortSignal) {
      void serial.execute(() => inner.submit(signal)).catch(() => undefined);
    }
  };
}

function wrapRequestCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkRequestCall): ZLinkYieldRequestCall {
  const yieldTurn = serial.currentTurn;
  return {
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    timeout(timeoutMs: number) {
      inner.timeout(timeoutMs);
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      const insideCurrentTurn = serial.isCurrentTurn;
      const pending = startRequestOnSerial(serial, () => ({ pending: inner.submit<TReply>(signal) }));
      return insideCurrentTurn ? pending : deliverOnSerial(serial, pending);
    },
    yield<TReply>(signal?: AbortSignal) {
      if (yieldTurn === undefined) {
        return Promise.reject(new ZLinkConfigurationException(
          'yield requires a framework Spot handler turn captured when the call object was created.'
        ));
      }
      const pending = startRequestOnSerial(serial, () => ({ pending: inner.submit<TReply>(signal) }));
      return yieldTurn.yieldPromise(pending);
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
  spot: SpotRef,
  message: unknown,
  spotRouterChannelIdForMesh: (meshName: string) => string,
  sourceSpotProvider?: () => ZLinkBackendSpot | undefined
): ZLinkSendCall {
  let selectedPacketName: string | undefined;
  return {
    packetName(packetName: string) {
      selectedPacketName = packetName;
      return this;
    },
    submit(signal?: AbortSignal) {
      void serial.execute(async () => {
        const spotRouteTarget = spotRefToSpotRouteTarget(spot, spotRouterChannelIdForMesh);
        const sourceSpot = sourceSpotProvider?.();
        if (sourceSpot !== undefined && transport.sendFromSpotToSpot !== undefined) {
          await transport.sendFromSpotToSpot(sourceSpot, spotRouteTarget, message, { packetName: selectedPacketName, signal });
          return;
        }
        await transport.sendToSpot(spotRouteTarget, message, { packetName: selectedPacketName, signal });
      }).catch(() => undefined);
    }
  };
}

function wrapRoutedSpotRequestCall(
  serial: ZLinkSpotSerialExecutor,
  transport: ZLinkSpotRoutedTransport,
  spot: SpotRef,
  request: unknown,
  spotRouterChannelIdForMesh: (meshName: string) => string,
  sourceSpotProvider?: () => ZLinkBackendSpot | undefined
): ZLinkYieldRequestCall {
  let selectedPacketName: string | undefined;
  let selectedTimeoutMs: number | undefined;
  const yieldTurn = serial.currentTurn;
  const begin = <TReply>(signal?: AbortSignal) => startRequestOnSerial<TReply>(serial, async () => {
    const spotRouteTarget = spotRefToSpotRouteTarget(spot, spotRouterChannelIdForMesh);
    const sourceSpot = sourceSpotProvider?.();
    if (sourceSpot !== undefined && transport.requestFromSpotToSpot !== undefined) {
      return {
        pending: transport.requestFromSpotToSpot<TReply>(sourceSpot, spotRouteTarget, request, {
          packetName: selectedPacketName,
          timeoutMs: selectedTimeoutMs,
          signal
        })
      };
    }
    return {
      pending: transport.requestToSpot<TReply>(spotRouteTarget, request, {
        packetName: selectedPacketName,
        timeoutMs: selectedTimeoutMs,
        signal
      })
    };
  });
  return {
    packetName(packetName: string) {
      selectedPacketName = packetName;
      return this;
    },
    timeout(timeoutMs: number) {
      selectedTimeoutMs = timeoutMs;
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      const insideCurrentTurn = serial.isCurrentTurn;
      const pending = begin<TReply>(signal);
      return insideCurrentTurn ? pending : deliverOnSerial(serial, pending);
    },
    yield<TReply>(signal?: AbortSignal) {
      if (yieldTurn === undefined) {
        return Promise.reject(new ZLinkConfigurationException(
          'yield requires a framework Spot handler turn captured when the call object was created.'
        ));
      }
      const pending = begin<TReply>(signal);
      return yieldTurn.yieldPromise(pending);
    }
  };
}

function spotRefToSpotRouteTarget(
  spot: SpotRef,
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

import type { Message } from '@zlink-systems/zlink';
import type { ZLinkBackendSpot } from '../backend/contracts';
import {
  ZLinkConfigurationException
} from '../configuration';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';

export interface ZLinkChannelClientTransport {
  send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): void;
  request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): void;
}

export interface ZLinkSpotPublisherClientTransport {
  publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): void;
}

export interface ZLinkRouteClientTransport {
  submit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): void;
  request<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  sendToSpot?(
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: { readonly packetName?: string; readonly signal?: AbortSignal }
  ): Promise<void>;
  requestToSpot?<TReply = unknown>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply>;
}

interface ZLinkChannelTransportRuntime {
  send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): void;
  request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): void;
  canRouteChannel(routerChannelId: string): boolean;
  canRoutePacketChannel(routerChannelId: string): boolean;
  routeSubmit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): void;
  routeRequest<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  routeSendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void>;
  routeSendFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void>;
  routeRequestToSpot<TReply>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
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

  send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): void {
    return this.requireManager().send(channelName, packetName, message, signal);
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.requireManager().request(channelName, packetName, request, timeoutMs, signal);
  }

  publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): void {
    return this.requireManager().publish(channelName, topic, packetName, event, signal);
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
    private readonly routeChannelPredicate: ((routerChannelId: string) => boolean) | undefined = undefined
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

  submit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): void {
    this.requireManager().routeSubmit(routerChannelId, targetNodeRid, packetName, message, signal);
  }

  async request<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.requireManager().routeRequest(routerChannelId, targetNodeRid, packetName, request, timeoutMs, signal);
  }

  async sendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: { readonly packetName?: string; readonly signal?: AbortSignal }
  ): Promise<void> {
    return this.requireManager().routeSendToSpot(spotRouteTarget, options.packetName, message, options.signal);
  }

  async sendFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: { readonly packetName?: string; readonly signal?: AbortSignal }
  ): Promise<void> {
    return this.requireManager().routeSendFromSpotToSpot(
      sourceSpot,
      spotRouteTarget,
      options.packetName,
      message,
      options.signal
    );
  }

  async requestToSpot<TReply = unknown>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply> {
    return this.requireManager().routeRequestToSpot<TReply>(
      spotRouteTarget,
      options.packetName,
      request,
      options.timeoutMs,
      options.signal
    );
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
}

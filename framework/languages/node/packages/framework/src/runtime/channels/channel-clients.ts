import type {
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkPublishCall,
  ZLinkRequestCall,
  ZLinkRouteClient,
  ZLinkSendCall,
  ZLinkSpotPublisherClient
} from '../../contracts';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration
} from '../configuration';
import type {
  ZLinkChannelClientTransport,
  ZLinkRouteClientTransport,
  ZLinkSpotPublisherClientTransport
} from './channel-transports';
import { throwIfAborted } from '../abort';

export class DefaultZLinkChannelClient implements ZLinkChannelClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  send(message: unknown): ZLinkSendCall {
    return this.sendInternal('', message);
  }

  request(request: unknown): ZLinkRequestCall {
    return this.requestInternal('', request);
  }

  sendToChannel(channelName: string, message: unknown): ZLinkSendCall {
    return this.sendInternal(channelName, message);
  }

  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall {
    return this.requestInternal(channelName, request);
  }

  private sendInternal(channelName: string, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => this.requireClientChannel(channelName),
      (packetName, signal) => this.requireTransport().send(channelName, packetName, message, signal)
    );
  }

  private requestInternal(channelName: string, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => this.requireClientChannel(channelName),
      (packetName, timeoutMs, signal) => this.requireTransport().request(channelName, packetName, request, timeoutMs, signal),
      this.defaultRequestTimeout(channelName)
    );
  }

  private defaultRequestTimeout(channelName: string): number {
    return this.registration.channels.get(channelName)?.requestTimeoutMs
      ?? this.registration.requestTimeoutMs
      ?? 30_000;
  }

  private requireClientChannel(channelName: string): void {
    if (!this.registration.channelClients.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a client capability.`);
    }
  }

  private requireTransport(): ZLinkChannelClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return this.transport;
  }
}

export class DefaultZLinkFanoutClient implements ZLinkFanoutClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  publish(topic: string, event: unknown): ZLinkPublishCall {
    return this.publishInternal(this.defaultPublisherChannel(), topic, event);
  }

  publishToChannel(channelName: string, topic: string, event: unknown): ZLinkPublishCall {
    return this.publishInternal(channelName, topic, event);
  }

  private publishInternal(channelName: string, topic: string, event: unknown): ZLinkPublishCall {
    return new DefaultZLinkPublishCall(
      () => this.requirePublisherChannel(channelName),
      (packetName, signal) => this.requireTransport().publish(channelName, topic, packetName, event, signal)
    );
  }

  private requirePublisherChannel(channelName: string): void {
    if (!this.registration.fanoutPublishers.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a publisher capability.`);
    }
  }

  private defaultPublisherChannel(): string {
    if (this.registration.fanoutPublishers.size === 1) {
      return [...this.registration.fanoutPublishers][0];
    }
    if (this.registration.fanoutPublishers.size === 0) {
      return '';
    }
    throw new ZLinkConfigurationException('Publish channel must be specified when more than one publisher channel is registered.');
  }

  private requireTransport(): ZLinkChannelClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return this.transport;
  }
}

export class DefaultZLinkRouteClient implements ZLinkRouteClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkRouteClientTransport
  ) {}

  sendToNode(routerChannelId: string, targetNodeRid: string, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => this.requireRouteChannel(routerChannelId),
      (packetName, signal) => this.requireTransport().send(routerChannelId, targetNodeRid, packetName, message, signal)
    );
  }

  requestToNode(routerChannelId: string, targetNodeRid: string, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => this.requireRouteChannel(routerChannelId),
      (packetName, timeoutMs, signal) => this.requireTransport().request(routerChannelId, targetNodeRid, packetName, request, timeoutMs, signal),
      this.defaultRequestTimeout(routerChannelId)
    );
  }

  private defaultRequestTimeout(routerChannelId: string): number {
    return this.registration.routeChannelOptions.get(routerChannelId)?.requestTimeoutMs
      ?? this.registration.requestTimeoutMs
      ?? 30_000;
  }

  private requireRouteChannel(routerChannelId: string): void {
    if (!this.registration.routeChannels.has(routerChannelId)) {
      throw new ZLinkConfigurationException(`Route channel '${routerChannelId}' is not registered.`);
    }
  }

  private requireTransport(): ZLinkRouteClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Route channel runtime is not started.');
    }
    return this.transport;
  }
}

export class DefaultZLinkSpotPublisherClient implements ZLinkSpotPublisherClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkSpotPublisherClientTransport
  ) {}

  publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall {
    const resolvedChannelName = channelName.length === 0 ? this.defaultSpotPublisherChannel() : channelName;
    return new DefaultZLinkPublishCall(
      () => this.requireSpotPublisherChannel(resolvedChannelName),
      (packetName, signal) => this.requireTransport().publish(resolvedChannelName, topic, packetName, event, signal)
    );
  }

  private requireSpotPublisherChannel(channelName: string): void {
    if (!this.registration.spotPublisherClients.has(channelName)) {
      throw new ZLinkConfigurationException(`SPOT publisher channel '${channelName}' is not attached.`);
    }
  }

  private defaultSpotPublisherChannel(): string {
    if (this.registration.spotPublisherClients.size === 1) {
      return [...this.registration.spotPublisherClients][0];
    }
    if (this.registration.spotPublisherClients.size === 0) {
      return '';
    }
    throw new ZLinkConfigurationException('SPOT publisher channel must be specified when more than one channel is attached.');
  }

  private requireTransport(): ZLinkSpotPublisherClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('SPOT publisher runtime is not started.');
    }
    return this.transport;
  }
}

class DefaultZLinkSendCall implements ZLinkSendCall {
  private packet?: string;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (packetName: string | undefined, signal?: AbortSignal) => Promise<void>
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  submit(signal?: AbortSignal): void {
    throwIfAborted(signal);
    this.validate();
    try {
      void this.submitter(this.packet, signal).catch(() => undefined);
    } catch {
      // One-way send failures are handled by the framework transport path.
    }
  }
}

class DefaultZLinkRequestCall implements ZLinkRequestCall {
  private packet?: string;
  private timeoutMs?: number;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: <TReply>(
      packetName: string | undefined,
      timeoutMs: number | undefined,
      signal?: AbortSignal
    ) => Promise<TReply>,
    private readonly defaultRequestTimeoutMs?: number
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply>(signal?: AbortSignal): Promise<TReply> {
    throwIfAborted(signal);
    this.validate();
    return this.submitter<TReply>(this.packet, this.timeoutMs ?? this.defaultRequestTimeoutMs, signal);
  }
}

class DefaultZLinkPublishCall implements ZLinkPublishCall {
  private packet?: string;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (packetName: string | undefined, signal?: AbortSignal) => Promise<void>
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  submit(signal?: AbortSignal): void {
    throwIfAborted(signal);
    this.validate();
    try {
      void this.submitter(this.packet, signal).catch(() => undefined);
    } catch {
      // One-way publish failures are handled by the framework transport path.
    }
  }
}

import type {
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkFanoutPublishCall,
  ZLinkMessageMetadata,
  ZLinkPublishCall,
  ZLinkPublishResult,
  ZLinkRequestCall,
  ZLinkRouteClient,
  ZLinkSendCall,
  ZLinkSpotPublisherClient,
  ZLinkSubmitResult
} from '../../contracts';
import type { SpotHandle } from '../spots/spot-handle';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration
} from '../configuration';
import { ZLinkSubmitStatus } from '../../contracts';
import type {
  ZLinkChannelClientTransport,
  ZLinkRouteClientTransport,
  ZLinkSpotPublisherClientTransport
} from './channel-transports';
import { throwIfAborted } from '../abort';
import {
  captureZLinkSpotSerialTurn,
  requireZLinkYieldTurn,
  type ZLinkSpotSerialTurn
} from '../execution';
import {
  requestToSpotHandle,
  sendToSpotHandle,
  type ZLinkSpotRoutedTransport
} from '../spots/spot-outbound';
import { resolveFrameworkPacketName } from '../messaging/packet-name';

export class DefaultZLinkChannelClient implements ZLinkChannelClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  sendToChannel(channelName: string, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => this.requireChannel(channelName),
      async (packetName, metadata, signal) =>
        normalizeSubmitResult(await this.requireTransport().send(
          channelName,
          packetName,
          message,
          signal,
          metadata
        ))
    );
  }

  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => this.requireChannel(channelName),
      (packetName, timeoutMs, metadata, signal) =>
        this.requireTransport().request(
          channelName,
          packetName,
          request,
          timeoutMs,
          signal,
          metadata
        ),
      this.defaultRequestTimeout(channelName)
    );
  }

  private defaultRequestTimeout(channelName: string): number {
    return this.registration.channels.get(channelName)?.requestTimeoutMs
      ?? this.registration.requestTimeoutMs
      ?? 30_000;
  }

  private requireChannel(channelName: string): void {
    if (!this.registration.channelClients.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel client '${channelName}' is not registered.`);
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

  publish(channelName: string, event: unknown): ZLinkFanoutPublishCall {
    const topic = resolveFrameworkPacketName(event, undefined, 'Fanout');
    return new DefaultZLinkFanoutPublishCall(
      () => this.requirePublisherChannel(channelName),
      async (signal) => ({
        status: (await this.requireTransport().publish(
          channelName,
          topic,
          topic,
          event,
          signal
        )).status
      })
    );
  }

  private requirePublisherChannel(channelName: string): void {
    if (!this.registration.fanoutPublishers.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a publisher capability.`);
    }
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
    private readonly transport?: ZLinkRouteClientTransport,
    private readonly spotRouterChannelIdForMesh: (meshName: string) => string = (meshName) => meshName
  ) {}

  sendToNode(meshName: string, targetNodeRid: string, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => this.requireMesh(meshName),
      async (packetName, metadata, signal) =>
        normalizeSubmitResult(await this.requireTransport().submit(
          meshName,
          targetNodeRid,
          packetName,
          message,
          signal,
          metadata
        ))
    );
  }

  requestToNode(meshName: string, targetNodeRid: string, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => this.requireMesh(meshName),
      (packetName, timeoutMs, metadata, signal) =>
        this.requireTransport().request(
          meshName,
          targetNodeRid,
          packetName,
          request,
          timeoutMs,
          signal,
          metadata
        ),
      this.defaultRequestTimeout(meshName)
    );
  }

  sendToChannel(channelName: string, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => { this.resolveMeshChannel(channelName); },
      async (packetName, metadata, signal) => {
        const meshName = this.resolveMeshChannel(channelName);
        return normalizeSubmitResult(await this.requireTransport().submitToChannel(
          meshName,
          channelName,
          packetName,
          message,
          signal,
          metadata
        ));
      }
    );
  }

  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => { this.resolveMeshChannel(channelName); },
      (packetName, timeoutMs, metadata, signal) => {
        const meshName = this.resolveMeshChannel(channelName);
        return this.requireTransport().requestToChannel(
          meshName,
          channelName,
          packetName,
          request,
          timeoutMs,
          signal,
          metadata
        );
      },
      this.defaultRequestTimeoutForChannel(channelName)
    );
  }

  sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => { this.requireSpotTransport(); },
      async (_packetName, metadata, signal) => {
        await sendToSpotHandle(this.requireSpotTransport(), spot, message, {
          signal,
          metadata,
          spotRouterChannelIdForMesh: this.spotRouterChannelIdForMesh
        });
        return { status: ZLinkSubmitStatus.Submitted };
      }
    );
  }

  requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => { this.requireSpotTransport(); },
      (_packetName, timeoutMs, metadata, signal) => requestToSpotHandle(
        this.requireSpotTransport(),
        spot,
        request,
        {
          timeoutMs,
          signal,
          metadata,
          spotRouterChannelIdForMesh: this.spotRouterChannelIdForMesh
        }
      ),
      this.registration.requestTimeoutMs ?? 30_000
    );
  }

  private defaultRequestTimeout(meshName: string): number {
    return this.registration.spotNodes.get(meshName)?.requestTimeoutMs
      ?? this.registration.routeChannelOptions.get(meshName)?.requestTimeoutMs
      ?? this.registration.requestTimeoutMs
      ?? 30_000;
  }

  private requireMesh(meshName: string): void {
    if (!this.registration.spotNodes.has(meshName) && !this.registration.routeChannels.has(meshName)) {
      throw new ZLinkConfigurationException(`RouteMesh '${meshName}' is not registered.`);
    }
  }

  private resolveMeshChannel(channelName: string): string {
    const matches = [...this.registration.spotNodes.entries()]
      .filter(([, mesh]) => Object.prototype.hasOwnProperty.call(mesh.meshChannels ?? {}, channelName))
      .map(([meshName]) => meshName);
    if (matches.length === 0) {
      throw new ZLinkConfigurationException(`RouteMesh channel '${channelName}' is not registered.`);
    }
    if (matches.length !== 1) {
      throw new ZLinkConfigurationException(
        `RouteMesh channel '${channelName}' must be unique across the Framework host.`
      );
    }
    return matches[0]!;
  }

  private defaultRequestTimeoutForChannel(channelName: string): number {
    const match = [...this.registration.spotNodes.entries()]
      .find(([, mesh]) => Object.prototype.hasOwnProperty.call(mesh.meshChannels ?? {}, channelName));
    return match?.[1].requestTimeoutMs
      ?? this.registration.requestTimeoutMs
      ?? 30_000;
  }

  private requireTransport(): ZLinkRouteClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Route channel runtime is not started.');
    }
    return this.transport;
  }

  private requireSpotTransport(): ZLinkSpotRoutedTransport {
    const transport = this.requireTransport();
    if (transport.sendToSpot === undefined || transport.requestToSpot === undefined) {
      throw new ZLinkConfigurationException('Route channel runtime does not support SpotHandle messaging.');
    }
    return transport as ZLinkSpotRoutedTransport;
  }

}

export class DefaultZLinkSpotPublisherClient implements ZLinkSpotPublisherClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkSpotPublisherClientTransport
  ) {}

  publish(meshName: string, channelName: string, topic: string, event: unknown): ZLinkPublishCall {
    return new DefaultZLinkPublishCall(
      () => this.requireMeshChannel(meshName, channelName),
      (packetName, metadata, signal) =>
        this.requireTransport().publish(meshName, channelName, topic, packetName, event, signal, metadata)
    );
  }

  private requireMeshChannel(meshName: string, channelName: string): void {
    const mesh = this.registration.spotNodes.get(meshName);
    if (mesh === undefined || !Object.prototype.hasOwnProperty.call(mesh.meshChannels ?? {}, channelName)) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' is not registered in RouteMesh '${meshName}'.`
      );
    }
  }

  private requireTransport(): ZLinkSpotPublisherClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('SPOT publisher runtime is not started.');
    }
    return this.transport;
  }
}

class DefaultZLinkSendCall implements ZLinkSendCall {
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (
      packetName: string | undefined,
      metadata: ReadonlyMap<string, string>,
      signal?: AbortSignal
    ) => Promise<ZLinkSubmitResult>
  ) {}

  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  metadata(keyOrMetadata: string | ZLinkMessageMetadata, value?: string): this {
    ensureNotExecuted(this.executed);
    if (typeof keyOrMetadata === 'string') {
      this.selectedMetadata.set(keyOrMetadata, value!);
    } else {
      for (const [key, selectedValue] of keyOrMetadata.values) {
        this.selectedMetadata.set(key, selectedValue);
      }
    }
    return this;
  }

  async submit(signal?: AbortSignal): Promise<ZLinkSubmitResult> {
    ensureNotExecuted(this.executed);
    this.validate();
    this.executed = true;
    throwIfAborted(signal);
    return await this.submitter(undefined, new Map(this.selectedMetadata), signal);
  }
}

function ensureNotExecuted(executed: boolean): void {
  if (executed) {
    throw new ZLinkConfigurationException('ZLink call has already been submitted.');
  }
}

function normalizeSubmitResult(result: void | ZLinkSubmitResult): ZLinkSubmitResult {
  return result ?? { status: ZLinkSubmitStatus.Submitted };
}

class DefaultZLinkRequestCall implements ZLinkRequestCall {
  private timeoutMs?: number;
  private readonly selectedMetadata = new Map<string, string>();
  private readonly turn: ZLinkSpotSerialTurn | undefined = captureZLinkSpotSerialTurn();

  constructor(
    private readonly validate: () => void,
    private readonly submitter: <TReply>(
      packetName: string | undefined,
      timeoutMs: number | undefined,
      metadata: ReadonlyMap<string, string>,
      signal?: AbortSignal
    ) => Promise<TReply>,
    private readonly defaultRequestTimeoutMs?: number
  ) {}

  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  metadata(keyOrMetadata: string | ZLinkMessageMetadata, value?: string): this {
    if (typeof keyOrMetadata === 'string') {
      this.selectedMetadata.set(keyOrMetadata, value!);
    } else {
      for (const [key, selectedValue] of keyOrMetadata.values) {
        this.selectedMetadata.set(key, selectedValue);
      }
    }
    return this;
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply>(signal?: AbortSignal): Promise<TReply> {
    throwIfAborted(signal);
    this.validate();
    return this.submitter<TReply>(
      undefined,
      this.timeoutMs ?? this.defaultRequestTimeoutMs,
      new Map(this.selectedMetadata),
      signal
    );
  }

  async yield<TReply>(signal?: AbortSignal): Promise<TReply> {
    const turn = requireZLinkYieldTurn(this.turn);
    throwIfAborted(signal);
    this.validate();
    const pending = this.submitter<TReply>(
      undefined,
      this.timeoutMs ?? this.defaultRequestTimeoutMs,
      new Map(this.selectedMetadata),
      signal
    );
    return turn.yieldPromise(pending);
  }
}

class DefaultZLinkPublishCall implements ZLinkPublishCall {
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (
      packetName: string | undefined,
      metadata: ReadonlyMap<string, string>,
      signal?: AbortSignal
    ) => ZLinkPublishResult | Promise<ZLinkPublishResult>
  ) {}

  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  metadata(keyOrMetadata: string | ZLinkMessageMetadata, value?: string): this {
    ensureNotExecuted(this.executed);
    if (typeof keyOrMetadata === 'string') {
      this.selectedMetadata.set(keyOrMetadata, value!);
    } else {
      for (const [key, selectedValue] of keyOrMetadata.values) {
        this.selectedMetadata.set(key, selectedValue);
      }
    }
    return this;
  }

  async submit(signal?: AbortSignal): Promise<ZLinkPublishResult> {
    ensureNotExecuted(this.executed);
    this.validate();
    this.executed = true;
    throwIfAborted(signal);
    return this.submitter(undefined, new Map(this.selectedMetadata), signal);
  }
}

class DefaultZLinkFanoutPublishCall implements ZLinkFanoutPublishCall {
  private executed = false;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (signal?: AbortSignal) => Promise<ZLinkSubmitResult>
  ) {}

  async submit(signal?: AbortSignal): Promise<ZLinkSubmitResult> {
    ensureNotExecuted(this.executed);
    this.validate();
    this.executed = true;
    throwIfAborted(signal);
    return await this.submitter(signal);
  }
}

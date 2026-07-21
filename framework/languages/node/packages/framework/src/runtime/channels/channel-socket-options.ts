import type { ZLinkSocketConfig } from '../../contracts';
import type {
  ZLinkSocketConfig as ZLinkRuntimeSocketConfig
} from '../../contracts/Configuration';
import {
  ZLinkConfigurationException
} from '../configuration';
import type {
  ZLinkBackendRouterSocket
} from '../backend/contracts';
import { requireValidSendTimeoutMs } from '../../contracts/Configuration/SendTimeoutValidation';

interface ZLinkChannelSocketOptionsRuntime {
  clientServerServerSocket(channelName: string): ZLinkBackendRouterSocket;
  routeMeshSocket(channelName: string): ZLinkBackendRouterSocket;
}

class ZLinkLiveSocketConfig implements ZLinkSocketConfig {
  constructor(private readonly socket: ZLinkBackendRouterSocket) {}

  get weight(): number {
    return this.socket.peerWeight;
  }

  set weight(value: number) {
    validatePeerWeight(value);
    this.socket.peerWeight = value;
  }

  get sendHighWaterMark(): number {
    return this.socket.sendHighWaterMark;
  }

  set sendHighWaterMark(value: number) {
    validateHighWaterMark(value, 'sendHighWaterMark');
    this.socket.sendHighWaterMark = value;
  }

  get receiveHighWaterMark(): number {
    return this.socket.receiveHighWaterMark;
  }

  set receiveHighWaterMark(value: number) {
    validateHighWaterMark(value, 'receiveHighWaterMark');
    this.socket.receiveHighWaterMark = value;
  }

  get sendTimeoutMs(): number {
    return this.socket.sendTimeoutMs;
  }

  set sendTimeoutMs(value: number) {
    validateSendTimeout(value);
    this.socket.sendTimeoutMs = value;
  }

  get maxMessageSize(): number {
    return this.socket.maxMessageSize;
  }

  set maxMessageSize(value: number) {
    validateMaxMessageSize(value);
    this.socket.maxMessageSize = value;
  }
}

class ZLinkServerRuntimeOptions {
  constructor(private readonly serverSocket: ZLinkSocketConfig) {}

  configureServerSocket(): ZLinkSocketConfig {
    return this.serverSocket;
  }
}

class ZLinkRouteRuntimeOptions {
  constructor(private readonly socket: ZLinkSocketConfig) {}

  configureSocket(): ZLinkSocketConfig {
    return this.socket;
  }
}

export class DefaultZLinkChannelRuntimeOptions {
  constructor(private readonly manager: () => ZLinkChannelSocketOptionsRuntime | undefined) {}

  serverChannel(channelName: string): ZLinkRuntimeSocketConfig {
    requireChannelName(channelName);
    return new ZLinkServerRuntimeOptions(
      new ZLinkLiveSocketConfig(this.requireManager().clientServerServerSocket(channelName))
    ).configureServerSocket();
  }

  routeChannel(channelName: string): ZLinkRuntimeSocketConfig {
    requireChannelName(channelName);
    return new ZLinkRouteRuntimeOptions(
      new ZLinkLiveSocketConfig(this.requireManager().routeMeshSocket(channelName))
    ).configureSocket();
  }

  private requireManager(): ZLinkChannelSocketOptionsRuntime {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return manager;
  }
}

function requireChannelName(channelName: string): void {
  if (channelName.trim().length === 0 || channelName.trim() !== channelName) {
    throw new ZLinkConfigurationException('Channel name must not be empty or padded.');
  }
}

function validatePeerWeight(value: number): void {
  if (!Number.isInteger(value) || value < 0 || value > 100) {
    throw new ZLinkConfigurationException('Weight must be between 0 and 100.');
  }
}

function validateHighWaterMark(value: number, label: string): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new ZLinkConfigurationException(`${label} must be a non-negative integer.`);
  }
}

function validateSendTimeout(value: number): void {
  requireValidSendTimeoutMs('sendTimeoutMs', value);
}

function validateMaxMessageSize(value: number): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new ZLinkConfigurationException('maxMessageSize must be a non-negative integer.');
  }
}

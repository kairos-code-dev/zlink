import type {
  ZLinkChannelRuntimeOptions,
  ZLinkClientServerChannelRuntimeOptions,
  ZLinkRouteMeshChannelRuntimeOptions,
  ZLinkSocketConfig
} from '../../contracts';
import {
  ZLinkConfigurationException
} from '../configuration';
import type {
  ZLinkBackendRouterSocket
} from '../backend/contracts';

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

class ZLinkClientServerRuntimeOptions implements ZLinkClientServerChannelRuntimeOptions {
  constructor(private readonly serverSocket: ZLinkSocketConfig) {}

  configureServerSocket(): ZLinkSocketConfig {
    return this.serverSocket;
  }
}

class ZLinkRouteMeshRuntimeOptions implements ZLinkRouteMeshChannelRuntimeOptions {
  constructor(private readonly socket: ZLinkSocketConfig) {}

  configureSocket(): ZLinkSocketConfig {
    return this.socket;
  }
}

export class DefaultZLinkChannelRuntimeOptions implements ZLinkChannelRuntimeOptions {
  constructor(private readonly manager: () => ZLinkChannelSocketOptionsRuntime | undefined) {}

  clientServerChannel(channelName: string): ZLinkClientServerChannelRuntimeOptions {
    requireChannelName(channelName);
    return new ZLinkClientServerRuntimeOptions(
      new ZLinkLiveSocketConfig(this.requireManager().clientServerServerSocket(channelName))
    );
  }

  routeMeshChannel(channelName: string): ZLinkRouteMeshChannelRuntimeOptions {
    requireChannelName(channelName);
    return new ZLinkRouteMeshRuntimeOptions(
      new ZLinkLiveSocketConfig(this.requireManager().routeMeshSocket(channelName))
    );
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
  if (!Number.isInteger(value) || value < -1) {
    throw new ZLinkConfigurationException('sendTimeoutMs must be -1 or a non-negative integer.');
  }
}

function validateMaxMessageSize(value: number): void {
  if (!Number.isInteger(value) || value < 0) {
    throw new ZLinkConfigurationException('maxMessageSize must be a non-negative integer.');
  }
}

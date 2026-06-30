import type { ZLinkSocketConfig } from '../Configuration';

export interface ZLinkClientServerChannelRuntimeOptions {
  configureServerSocket(): ZLinkSocketConfig;
}

export interface ZLinkRouteMeshChannelRuntimeOptions {
  configureSocket(): ZLinkSocketConfig;
}

export interface ZLinkChannelRuntimeOptions {
  clientServerChannel(channelName: string): ZLinkClientServerChannelRuntimeOptions;
  routeMeshChannel(channelName: string): ZLinkRouteMeshChannelRuntimeOptions;
}

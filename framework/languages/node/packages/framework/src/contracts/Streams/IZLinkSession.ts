import type { Message, RoutingId, ZlinkStreamHeader } from '../Common';
import type { ZLinkSessionActors } from './IZLinkSessionActor';
import type { ZLinkStream } from './IZLinkStream';
import type { ZLinkStreamError } from './ZLinkStreamError';

export interface ZLinkSession {
  readonly context: ZLinkSessionContext;
  onConnected?(context: ZLinkSessionContext): Promise<void>;
  onDisconnected?(context: ZLinkSessionContext): Promise<void>;
  onError?(context: ZLinkSessionContext, error: ZLinkStreamError): Promise<void>;
  onDispatch?(header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionContext {
  readonly sessionId: string;
  readonly routingId?: RoutingId;
  readonly localAddr?: string;
  readonly remoteAddr?: string;
  readonly stream: ZLinkStream;
  readonly client: ZLinkSessionClient;
  readonly actors: ZLinkSessionActors;
  close(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionClient {
  send<TMessage>(message: TMessage): ZLinkSessionSendCall;
  reply<TMessage>(message: TMessage): ZLinkSessionReplyCall;
}

export interface ZLinkSessionSendCall {
  metadata(key: string, value: string): this;
  packetName(packetName: string): this;
  compress(enabled?: boolean): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSessionReplyCall {
  metadata(key: string, value: string): this;
  compress(enabled?: boolean): this;
  submit(signal?: AbortSignal): Promise<void>;
}

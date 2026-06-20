import type { ZLinkDispatchMode } from './ZLinkDispatchMode';
import type { Type } from '../Common';

export interface ZLinkDispatchOptions {
  mode?: ZLinkDispatchMode;
  unhandled?: ZLinkUnhandledDispatchOptions;
  diagnostics?: ZLinkDiagnosticsOptions;
  messageDispatchErrorObserverType?: Type<ZLinkMessageDispatchErrorObserver>;
}

export interface ZLinkDispatchOptionsBuilder {
  setMessageDispatchErrorObserver(
    observerType: Type<ZLinkMessageDispatchErrorObserver>
  ): this;
}

export interface ZLinkMessageDispatchErrorObserver {
  onDispatchError(error: ZLinkMessageDispatchErrorEvent): Promise<void> | void;
}

export interface ZLinkMessageDispatchErrorEvent {
  readonly surface: ZLinkDispatchErrorSurface;
  readonly messageKind: ZLinkDispatchMessageKind;
  readonly reason: ZLinkDispatchErrorReason;
  readonly action: ZLinkDispatchErrorAction;
  readonly packetName?: string;
  readonly channelName?: string;
  readonly topic?: string;
  readonly spotRid?: string;
  readonly actorId?: string;
  readonly sourceRid?: string;
  readonly correlationId?: string;
  readonly error?: unknown;
}

export interface ZLinkUnhandledDispatchOptions {
  action?: ZLinkUnhandledDispatchAction;
}

export interface ZLinkDiagnosticsOptions {
  messageFlowLogMode?: ZLinkMessageFlowLogMode;
}

export enum ZLinkUnhandledDispatchAction {
  Ignore = 'ignore',
  Warn = 'warn',
  Throw = 'throw'
}

export enum ZLinkMessageFlowLogMode {
  Off = 'off',
  Metadata = 'metadata',
  Verbose = 'verbose'
}

export enum ZLinkDispatchErrorSurface {
  Channel = 'channel',
  DealerMeshChannel = 'dealerMeshChannel',
  RouteMeshChannel = 'routeMeshChannel',
  SpotRoute = 'spotRoute',
  SpotSubscription = 'spotSubscription',
  SpotActor = 'spotActor',
  StreamSession = 'streamSession'
}

export enum ZLinkDispatchMessageKind {
  Request = 'request',
  Send = 'send',
  Publish = 'publish',
  Response = 'response',
  Error = 'error',
  ActorRequest = 'actorRequest',
  ActorSend = 'actorSend'
}

export enum ZLinkDispatchErrorReason {
  HandlerMissing = 'handlerMissing',
  PayloadDecodeFailed = 'payloadDecodeFailed',
  HandlerException = 'handlerException',
  InvalidFrame = 'invalidFrame',
  ReplyPathMissing = 'replyPathMissing',
  UnexpectedReply = 'unexpectedReply'
}

export enum ZLinkDispatchErrorAction {
  ReplyError = 'replyError',
  Drop = 'drop'
}

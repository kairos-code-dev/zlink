import type { ZLinkDispatchMode } from './ZLinkDispatchMode';
import type { Type } from '../Common';

export interface ZLinkDispatchOptions {
  mode?: ZLinkDispatchMode;
  unhandled?: ZLinkUnhandledDispatchOptions;
  diagnostics?: ZLinkDiagnosticsOptions;
  messageDispatchErrorObserverType?: Type<ZLinkMessageDispatchErrorObserver>;
  messageFlowObserverType?: Type<ZLinkMessageFlowObserver>;
}

export interface ZLinkDispatchOptionsBuilder {
  setMessageDispatchErrorObserver(
    observerType: Type<ZLinkMessageDispatchErrorObserver>
  ): this;

  setMessageFlowObserver(observerType: Type<ZLinkMessageFlowObserver>): this;

  /** Fluent diagnostics/tracing config (builder-chain only). */
  messageFlow(mode: ZLinkMessageFlowLogMode): this;

  traceSampleRate(rate: number): this;

  includeMessageSizes(include: boolean): this;

  /** Send tracing/error logs to a dedicated file (separated from app logs). */
  traceLogFile(path: string): this;

  /** Node identity stamped on every trace line (node=) for cross-node aggregation. */
  traceNodeId(id: string): this;
}

/**
 * A success-path transition in a message's lifecycle. Errors are reported separately
 * via ZLinkMessageDispatchErrorEvent. received/dispatched/replied are inbound;
 * sent/replyReceived are outbound.
 */
export enum ZLinkMessageFlowPhase {
  Received = 'received',
  Dispatched = 'dispatched',
  Replied = 'replied',
  Dropped = 'dropped',
  Sent = 'sent',
  ReplyReceived = 'replyReceived'
}

export interface ZLinkMessageFlowEvent {
  readonly phase: ZLinkMessageFlowPhase;
  readonly surface: ZLinkDispatchErrorSurface;
  readonly messageKind: ZLinkDispatchMessageKind;
  readonly packetName?: string;
  readonly channelName?: string;
  readonly topic?: string;
  readonly correlationId?: string;
  readonly sourceRid?: string;
  readonly spotRid?: string;
  readonly actorId?: string;
  readonly messageSize?: number;
}

export interface ZLinkMessageFlowObserver {
  onMessageFlow(flow: ZLinkMessageFlowEvent): Promise<void> | void;
}

/**
 * Resolve from the runtime to turn message-flow tracing on/off (or change verbosity)
 * at runtime without a restart. Read live by every dispatch surface.
 */
export interface ZLinkMessageFlowControl {
  setMessageFlowMode(mode: ZLinkMessageFlowLogMode): void;
  messageFlowMode(): ZLinkMessageFlowLogMode;
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
  sampleRate?: number;
  includeMessageSizes?: boolean;
  /** When set, tracing/error logs go to this dedicated file (separated from app logs). */
  logFile?: string;
  /** Node/runtime identity stamped on each trace line. */
  nodeId?: string;
}

export enum ZLinkUnhandledDispatchAction {
  Ignore = 'ignore',
  Warn = 'warn',
  Throw = 'throw'
}

export enum ZLinkMessageFlowLogMode {
  Off = 'off',
  ErrorsOnly = 'errorsOnly',
  KeyTransitions = 'keyTransitions',
  Verbose = 'verbose',
  Diagnostic = 'diagnostic'
}

/** Severity rank for the mode ladder (off < errorsOnly < keyTransitions < verbose < diagnostic). */
export const MESSAGE_FLOW_MODE_RANK: Record<ZLinkMessageFlowLogMode, number> = {
  [ZLinkMessageFlowLogMode.Off]: 0,
  [ZLinkMessageFlowLogMode.ErrorsOnly]: 1,
  [ZLinkMessageFlowLogMode.KeyTransitions]: 2,
  [ZLinkMessageFlowLogMode.Verbose]: 3,
  [ZLinkMessageFlowLogMode.Diagnostic]: 4
};

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

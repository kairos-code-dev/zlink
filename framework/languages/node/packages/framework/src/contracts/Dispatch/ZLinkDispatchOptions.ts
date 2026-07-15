import type { Type } from '../Common';

export interface ZLinkDispatchOptions {
  readonly unhandled: ZLinkUnhandledDispatchOptions;
  readonly diagnostics: ZLinkDiagnosticsOptions;
}

export interface ZLinkDispatchOptionsBuilder {
  setMessageFlowObserver(observerType: Type<ZLinkMessageFlowObserver>): this;

  /** Fluent diagnostics/tracing config (builder-chain only). */
  messageFlow(mode: ZLinkMessageFlowLogMode): this;

  traceSampleRate(rate: number): this;

  includeMessageSizes(include: boolean): this;

  /** Send tracing/error logs to a dedicated file (separated from app logs). */
  traceLogFile(path: string): this;

  /** Human-readable label stamped on every trace line (label=) for aggregation. */
  traceLabel(label: string): this;
}

/**
 * A message lifecycle outcome. Error outcomes carry the dispatch error fields on the
 * same typed event used by the success path.
 */
export enum ZLinkMessageFlowOutcome {
  Received = 'received',
  Dispatched = 'dispatched',
  Replied = 'replied',
  Dropped = 'dropped',
  Sent = 'sent',
  ReplyReceived = 'replyReceived',
  Error = 'error'
}

export interface ZLinkMessageFlowEvent {
  readonly outcome: ZLinkMessageFlowOutcome;
  readonly surface: ZLinkDispatchErrorSurface;
  readonly messageKind: ZLinkDispatchMessageKind;
  readonly packetName?: string;
  readonly channelName?: string;
  readonly topic?: string;
  readonly correlationId?: string;
  readonly sourceRid?: string;
  readonly peerRid?: string;
  readonly socketRole?: string;
  readonly effectiveMode: ZLinkMessageFlowLogMode;
  readonly flowId: string;
  readonly flowOrigin: import('../Eventing/Contracts').ZLinkFlowOrigin;
  readonly spotRid?: string;
  readonly actorId?: string;
  readonly messageSize?: number;
  readonly errorReason?: ZLinkDispatchErrorReason;
  readonly errorAction?: ZLinkDispatchErrorAction;
  readonly errorType?: string;
  readonly errorMessage?: string;
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

export interface ZLinkDispatchFailure {
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
  readonly flowId?: string;
  readonly flowOrigin?: import('../Eventing/Contracts').ZLinkFlowOrigin;
  readonly errorType?: string;
  readonly errorMessage?: string;
}

export interface ZLinkUnhandledDispatchOptions {
  request: ZLinkUnhandledDispatchAction;
  send: ZLinkUnhandledDispatchAction;
  publish: ZLinkUnhandledDispatchAction;
}

export interface ZLinkDiagnosticsOptions {
  messageFlow: ZLinkMessageFlowLogMode;
  sampleRate: number;
  includeMessageSizes: boolean;
  /** When set, tracing/error logs go to this dedicated file (separated from app logs). */
  logFile?: string;
  /** Human-readable runtime label stamped on each trace line. */
  label?: string;
}

export enum ZLinkUnhandledDispatchAction {
  ReplyError = 'replyError',
  LogAndDrop = 'logAndDrop',
  Drop = 'drop',
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
  FailCaller = 'failCaller',
  Drop = 'drop'
}

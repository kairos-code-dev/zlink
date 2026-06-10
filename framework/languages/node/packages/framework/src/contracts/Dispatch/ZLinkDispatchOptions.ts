import type { ZLinkDispatchMode } from './ZLinkDispatchMode';

export interface ZLinkDispatchOptions {
  mode?: ZLinkDispatchMode;
  unhandled?: ZLinkUnhandledDispatchOptions;
  diagnostics?: ZLinkDiagnosticsOptions;
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

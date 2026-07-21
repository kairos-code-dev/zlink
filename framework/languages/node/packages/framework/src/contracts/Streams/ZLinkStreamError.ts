export enum ZLinkStreamSessionError {
  Internal = 'internal',
  TransportError = 'transportError'
}

export interface ZLinkStreamDiagnostic {
  readonly nativeCode?: number;
  readonly message?: string | undefined;
}

export interface ZLinkStreamError {
  readonly error: ZLinkStreamSessionError;
  readonly diagnostic?: ZLinkStreamDiagnostic;
}

import { createHash } from 'node:crypto';

export const PacketNames = {
  profileRequest: 'ProfileRequest',
  profileCommand: 'ProfileCommand',
  payloadRequest: 'PayloadRequest',
  missingProfileRequest: 'MissingProfileRequest',
  missingProfileCommand: 'MissingProfileCommand'
} as const;

export interface ProfileRequest {
  readonly value: string;
  readonly marker?: string;
}

export interface ProfileReply {
  readonly value: string;
  readonly providerRid: string;
}

export interface ProfileCommand {
  readonly commandId: string;
}

export interface EvidenceWaitRequest {
  readonly contains: string;
  readonly timeoutMilliseconds?: number;
}

export interface WeightWaitRequest {
  readonly expected: number;
  readonly timeoutMilliseconds?: number;
}

export interface PayloadRequest {
  readonly marker: string;
  readonly payload: string;
}

export interface PayloadReply {
  readonly marker: string;
  readonly length: number;
  readonly sha256: string;
}

export interface RouteMissingResult {
  readonly failed: boolean;
}

export interface RequestFailureResult {
  readonly failed: boolean;
  readonly failureType: string;
}

export interface TimeoutRequestResult {
  readonly status: number;
  readonly timedOut: boolean;
}

export function sha256Hex(value: string): string {
  return createHash('sha256').update(value, 'utf8').digest('hex').toUpperCase();
}

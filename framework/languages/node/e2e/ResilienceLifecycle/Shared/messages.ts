import { createHash } from 'node:crypto';

export const PacketNames = {
  profileReq: 'ProfileReq',
  profileMsg: 'ProfileMsg',
  payloadReq: 'PayloadReq',
  missingProfileReq: 'MissingProfileReq',
  missingProfileMsg: 'MissingProfileMsg'
} as const;

export interface ProfileReq {
  readonly value: string;
  readonly marker?: string;
}

export interface ProfileRes {
  readonly value: string;
  readonly providerRid: string;
}

export interface ProfileMsg {
  readonly commandId: string;
}

export interface EvidenceWaitReq {
  readonly contains: string;
  readonly timeoutMilliseconds?: number;
}

export interface WeightWaitReq {
  readonly expected: number;
  readonly timeoutMilliseconds?: number;
}

export interface PayloadReq {
  readonly marker: string;
  readonly payload: string;
}

export interface PayloadRes {
  readonly marker: string;
  readonly length: number;
  readonly sha256: string;
}

export interface RouteMissingRes {
  readonly failed: boolean;
}

export interface RequestFailureRes {
  readonly failed: boolean;
  readonly failureType: string;
}

export interface TimeoutRes {
  readonly status: number;
  readonly timedOut: boolean;
}

export function sha256Hex(value: string): string {
  return createHash('sha256').update(value, 'utf8').digest('hex').toUpperCase();
}

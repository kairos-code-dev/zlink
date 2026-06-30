import { createHash } from 'node:crypto';

export const PacketNames = {
  profileReq: 'ProfileReq',
  profileMsg: 'ProfileMsg',
  payloadReq: 'PayloadReq',
  workflowReq: 'WorkflowReq',
  scenarioRouteReq: 'ScenarioRouteReq',
  missingProfileReq: 'MissingProfileReq',
  missingProfileMsg: 'MissingProfileMsg'
} as const;

export interface ProfileReq {
  readonly value: string;
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

export interface PayloadReq {
  readonly marker: string;
  readonly payload: string;
}

export interface PayloadRes {
  readonly marker: string;
  readonly length: number;
  readonly sha256: string;
}

export interface WorkflowReq {
  readonly value: string;
}

export interface WorkflowRes {
  readonly value: string;
  readonly providerRid: string;
}

export interface ScenarioRouteReq {
  readonly value: string;
}

export interface ScenarioRouteRes {
  readonly value: string;
  readonly providerRid: string;
  readonly sourceRid: string;
}

export interface RouteMissingRes {
  readonly failed: boolean;
}

export interface RequestFailureRes {
  readonly failed: boolean;
  readonly failureType: string;
}

export function sha256Hex(value: string): string {
  return createHash('sha256').update(value, 'utf8').digest('hex').toUpperCase();
}

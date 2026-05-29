// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../core';

export const MonitorSourceKind = Object.freeze({ Socket: 1, SpotPub: 3, SpotSub: 4 } as const);
export type MonitorSourceKindValue = typeof MonitorSourceKind[keyof typeof MonitorSourceKind];

export const MonitorEventType = Object.freeze({
  Connected: 0x0001,
  ConnectDelayed: 0x0002,
  ConnectRetried: 0x0004,
  Listening: 0x0008,
  BindFailed: 0x0010,
  Accepted: 0x0020,
  AcceptFailed: 0x0040,
  Closed: 0x0080,
  CloseFailed: 0x0100,
  Disconnected: 0x0200,
  MonitorStopped: 0x0400,
  HandshakeFailedNoDetail: 0x0800,
  ConnectionReady: 0x1000,
  HandshakeFailedProtocol: 0x2000,
  HandshakeFailedAuth: 0x4000,
  PeerWeightChanged: 0x8000
} as const);
export type MonitorEventType = typeof MonitorEventType[keyof typeof MonitorEventType];

export interface MonitorStatus {
  readonly sourceKind: MonitorSourceKindValue;
  readonly stateFlags: number;
  readonly detailFlags: number;
  readonly sndPendingMsgs: bigint;
  readonly rcvPendingMsgs: bigint;
  readonly autoHwmEnabled: boolean;
  readonly autoHwmProfile: number;
  readonly autoHwmRole: number;
  readonly autoHwmPolicyClass: number;
  readonly autoHwmUnitBudgetBytes: bigint;
  readonly autoHwmSizeCap: number;
  readonly autoHwmSocketMessageSlots: bigint;
  readonly autoHwmEffectiveMessageBytes: bigint;
  readonly autoHwmAppliedSndHwm: number;
  readonly autoHwmAppliedRcvHwm: number;
  readonly autoHwmEffectiveSndBuf: number;
  readonly autoHwmEffectiveRcvBuf: number;
  readonly autoHwmLastRecalcMs: bigint;
  readonly autoHwmLastRecalcReason: number;
  readonly autoHwmSendBlockedRatioPpm: number;
  readonly autoHwmDeferredSndHwm: number;
  readonly autoHwmDeferredRcvHwm: number;
  isReady(): boolean;
}

export interface MonitorStatusRaw {
  sourceKind: number;
  stateFlags: number;
  detailFlags: number;
  sndPendingMsgs: number | bigint;
  rcvPendingMsgs: number | bigint;
  autoHwmEnabled: boolean;
  autoHwmProfile: number;
  autoHwmRole: number;
  autoHwmPolicyClass: number;
  autoHwmUnitBudgetBytes: number | bigint;
  autoHwmSizeCap: number;
  autoHwmSocketMessageSlots: number | bigint;
  autoHwmEffectiveMessageBytes: number | bigint;
  autoHwmAppliedSndHwm: number;
  autoHwmAppliedRcvHwm: number;
  autoHwmEffectiveSndBuf?: number;
  autoHwmEffectiveRcvBuf?: number;
  autoHwmLastRecalcMs: number | bigint;
  autoHwmLastRecalcReason: number;
  autoHwmSendBlockedRatioPpm: number;
  autoHwmDeferredSndHwm: number;
  autoHwmDeferredRcvHwm: number;
}

export interface MonitorEventValueRaw {
  event: number;
  value: number;
  routingId?: Buffer | null;
  local?: string;
  remote?: string;
  localAddr?: string;
  remoteAddr?: string;
}

const MONITOR_EVENT_CREATE_TOKEN = Symbol('MonitorEvent.create');

export class MonitorEvent {
  readonly event: MonitorEventType;
  readonly value: number;
  readonly routingId: RoutingId | null;
  readonly localAddr: string;
  readonly remoteAddr: string;

  private constructor(token: symbol, raw: MonitorEventValueRaw) {
    if (token !== MONITOR_EVENT_CREATE_TOKEN) {
      throw new TypeError('MonitorEvent values are created by monitor recv operations');
    }
    this.event = raw.event as MonitorEventType;
    this.value = raw.value;
    this.routingId = raw.routingId && raw.routingId.length > 0 ? RoutingId.from(raw.routingId) : null;
    this.localAddr = raw.localAddr ?? raw.local ?? '';
    this.remoteAddr = raw.remoteAddr ?? raw.remote ?? '';
  }

  /** @internal */
  static create(raw: MonitorEventValueRaw): MonitorEvent {
    return new MonitorEvent(MONITOR_EVENT_CREATE_TOKEN, raw);
  }
}

Object.freeze(MonitorEvent);

export interface MonitorSocket {
  recv(flags?: number): MonitorEvent | null;
  onEvent(handler: (event: MonitorEvent) => void): void;
  status(): MonitorStatus;
  close(): void;
}

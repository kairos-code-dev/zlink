// SPDX-License-Identifier: MPL-2.0

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

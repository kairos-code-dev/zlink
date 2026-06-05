// SPDX-License-Identifier: MPL-2.0

import {
  type MonitorSourceKindValue,
  type MonitorStatus
} from '../../contracts/eventing';
import type { MonitorStatusRaw } from './monitor_raw';

const MONITOR_STATE_READY = 1 << 0;

export function materializeMonitorStatus(raw: MonitorStatusRaw): MonitorStatus {
  return {
    sourceKind: raw.sourceKind as MonitorSourceKindValue,
    stateFlags: raw.stateFlags,
    detailFlags: raw.detailFlags,
    sndPendingMsgs: BigInt(raw.sndPendingMsgs),
    rcvPendingMsgs: BigInt(raw.rcvPendingMsgs),
    autoHwmEnabled: raw.autoHwmEnabled,
    autoHwmProfile: raw.autoHwmProfile,
    autoHwmRole: raw.autoHwmRole,
    autoHwmPolicyClass: raw.autoHwmPolicyClass,
    autoHwmUnitBudgetBytes: BigInt(raw.autoHwmUnitBudgetBytes),
    autoHwmSizeCap: raw.autoHwmSizeCap,
    autoHwmSocketMessageSlots: BigInt(raw.autoHwmSocketMessageSlots),
    autoHwmEffectiveMessageBytes: BigInt(raw.autoHwmEffectiveMessageBytes),
    autoHwmAppliedSndHwm: raw.autoHwmAppliedSndHwm,
    autoHwmAppliedRcvHwm: raw.autoHwmAppliedRcvHwm,
    autoHwmEffectiveSndBuf: raw.autoHwmEffectiveSndBuf ?? 0,
    autoHwmEffectiveRcvBuf: raw.autoHwmEffectiveRcvBuf ?? 0,
    autoHwmLastRecalcMs: BigInt(raw.autoHwmLastRecalcMs),
    autoHwmLastRecalcReason: raw.autoHwmLastRecalcReason,
    autoHwmSendBlockedRatioPpm: raw.autoHwmSendBlockedRatioPpm,
    autoHwmDeferredSndHwm: raw.autoHwmDeferredSndHwm,
    autoHwmDeferredRcvHwm: raw.autoHwmDeferredRcvHwm,
    isReady(): boolean {
      return (this.stateFlags & MONITOR_STATE_READY) !== 0;
    }
  };
}

import type { ZLinkMeter, ZLinkMeterProvider, ZLinkMetricAttributes } from '../../contracts';
import { ZLinkMeters } from '../../contracts';
import { metrics as openTelemetryMetrics } from '@opentelemetry/api';

export class ZLinkRuntimeMetrics {
  private readonly meter?: ZLinkMeter;
  private readonly counters = new Map<string, ReturnType<ZLinkMeter['createCounter']>>();
  private readonly upDown = new Map<string, ReturnType<ZLinkMeter['createUpDownCounter']>>();
  private readonly histograms = new Map<string, ReturnType<ZLinkMeter['createHistogram']>>();

  constructor(provider?: ZLinkMeterProvider) {
    this.meter = (provider ?? openTelemetryMetrics).getMeter(ZLinkMeters.Framework) as ZLinkMeter;
  }

  count(name: string, value = 1, attributes?: ZLinkMetricAttributes): void {
    if (this.meter === undefined) return;
    let instrument = this.counters.get(name);
    if (instrument === undefined) {
      instrument = this.meter.createCounter(name, { unit: metricUnit(name) });
      this.counters.set(name, instrument);
    }
    instrument.add(value, attributes);
  }

  change(name: string, value: number, attributes?: ZLinkMetricAttributes): void {
    if (this.meter === undefined) return;
    let instrument = this.upDown.get(name);
    if (instrument === undefined) {
      instrument = this.meter.createUpDownCounter(name, { unit: metricUnit(name) });
      this.upDown.set(name, instrument);
    }
    instrument.add(value, attributes);
  }

  duration(name: string, seconds: number, attributes?: ZLinkMetricAttributes): void {
    this.histogram(name, seconds, 's', attributes);
  }

  histogram(name: string, value: number, unit: string, attributes?: ZLinkMetricAttributes): void {
    if (this.meter === undefined) return;
    let instrument = this.histograms.get(name);
    if (instrument === undefined) {
      instrument = this.meter.createHistogram(name, { unit });
      this.histograms.set(name, instrument);
    }
    instrument.record(value, attributes);
  }

  enabled(): boolean {
    return this.meter !== undefined;
  }
}

function metricUnit(name: string): string {
  if (name.endsWith('.duration') || name.endsWith('.latency') || name.endsWith('.lateness')) return 's';
  if (name.endsWith('.bytes')) return 'By';
  if (name.includes('connection')) return '{connection}';
  if (name === 'zlink.actor.transfers') return '{transfer}';
  if (name.includes('.actor.')) return '{actor}';
  if (name.includes('.queue.depth')) return '{item}';
  if (name.includes('.peers')) return '{peer}';
  if (name.includes('.failures')) return '{failure}';
  if (name.includes('.errors')) return '{error}';
  if (name.includes('.conflicts')) return '{write}';
  if (name.includes('.spot.')) return '{spot}';
  if (name.includes('.request.')) return '{request}';
  if (name.includes('.message')) return '{message}';
  return '{event}';
}

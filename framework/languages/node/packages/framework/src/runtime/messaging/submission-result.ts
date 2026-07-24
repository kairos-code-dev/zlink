import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';

export enum ZLinkSubmitStatus {
  Submitted = 'submitted',
  Backpressured = 'backpressured',
  TimedOut = 'timedOut',
  TargetNotFound = 'targetNotFound',
  RouteNotConnected = 'routeNotConnected',
  Shutdown = 'shutdown'
}

export interface ZLinkSubmitResult {
  readonly status: ZLinkSubmitStatus;
}

export function requireOneWayCompletion(
  result: ZLinkSubmitResult,
  operation: string,
  notFoundKind: ZLinkFrameworkErrorKind = ZLinkFrameworkErrorKind.RequestTargetNotFound
): void {
  switch (result.status) {
    case ZLinkSubmitStatus.Submitted:
      return;
    case ZLinkSubmitStatus.TimedOut:
    case ZLinkSubmitStatus.Backpressured:
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.DeadlineExceeded,
        `${operation} did not obtain admission before its deadline.`,
        true
      );
    case ZLinkSubmitStatus.TargetNotFound:
      throw new ZLinkFrameworkException(
        notFoundKind,
        `${operation} target was not found.`
      );
    case ZLinkSubmitStatus.RouteNotConnected:
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RouteNotConnected,
        `${operation} route is not connected.`,
        true
      );
    case ZLinkSubmitStatus.Shutdown:
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RuntimeShutdown,
        `${operation} was rejected because the runtime is shutting down.`
      );
  }
}

export function requirePublishCompletion(
  result: ZLinkSubmitResult,
  operation: string
): void {
  requireOneWayCompletion(result, operation);
}

export function throwAlreadySubmitted(operation: string): never {
  throw new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.AlreadySubmitted,
    `${operation} has already been submitted.`
  );
}

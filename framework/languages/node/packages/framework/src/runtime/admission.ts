import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../contracts';

export class ZLinkRuntimeAdmissionGate {
  private open = true;

  get acceptsNewWork(): boolean {
    return this.open;
  }

  close(): void {
    this.open = false;
  }

  requireRequest(operation: string): void {
    if (this.open) return;
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.RequestRejected,
      `${operation} was rejected because the framework is draining.`
    );
  }

  requireActorCreate(actorId: string): void {
    if (this.open) return;
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorCreateRejected,
      `Actor '${actorId}' create request was rejected because the framework is draining.`
    );
  }
}

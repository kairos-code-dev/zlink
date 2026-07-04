import { NotificationDeliveryLog } from '../../../../../notification-delivery-log';

class SupportNotificationPublisher {
  constructor(private readonly log = new NotificationDeliveryLog()) {}

  publish(name: string, conversationId: string): void {
    this.log.append(`message flow ${name} conversation=${conversationId}`);
  }
}

export { SupportNotificationPublisher };

import { appendFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

class NotificationDeliveryLog {
  constructor(private readonly fileName = `${process.env.SUPPORTCHAT_LOG_DIR ?? 'logs'}/flow-support.log`) {}

  append(message: string): void {
    mkdirSync(dirname(this.fileName), { recursive: true });
    appendFileSync(this.fileName, `${new Date().toISOString()} ${message}\n`);
  }
}

export { NotificationDeliveryLog };

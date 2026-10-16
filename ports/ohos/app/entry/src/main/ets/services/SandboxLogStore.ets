import fs from '@ohos.file.fs';
import type common from '@ohos.app.ability.common';

export class SandboxLogStore {
  static save(context: common.Context, title: string, text: string): string {
    const fileName = `${SandboxLogStore.safeName(title)}-${Date.now()}.log`;
    const path = `${context.filesDir}/${fileName}`;
    const file = fs.openSync(path, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE);
    try {
      fs.writeSync(file.fd, text);
    } finally {
      fs.closeSync(file);
    }
    return path;
  }

  private static safeName(value: string): string {
    const trimmed = value.trim();
    if (trimmed.length === 0) {
      return 'session';
    }
    return trimmed.replace(/[^A-Za-z0-9_.-]/g, '_').slice(0, 48);
  }
}

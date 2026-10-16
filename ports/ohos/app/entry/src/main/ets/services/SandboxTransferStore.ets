import fs from '@ohos.file.fs';
import type common from '@ohos.app.ability.common';

export class SandboxTransferStore {
  static downloadPath(context: common.Context, remotePath: string): string {
    const dir = `${context.filesDir}/ssh_downloads`;
    SandboxTransferStore.ensureDirectory(dir);
    return `${dir}/${SandboxTransferStore.safeFileName(remotePath)}`;
  }

  private static ensureDirectory(path: string): void {
    try {
      fs.mkdirSync(path, true);
    } catch (error) {
      if (`${error}`.indexOf('File exists') < 0) {
        throw new Error(`${error}`);
      }
    }
  }

  private static safeFileName(value: string): string {
    const last = value.split('/').pop() || `download-${Date.now()}`;
    const decoded = decodeURIComponent(last);
    const name = decoded.replace(/[^A-Za-z0-9_.-]/g, '_').slice(0, 64);
    return name.length > 0 ? name : `download-${Date.now()}`;
  }
}

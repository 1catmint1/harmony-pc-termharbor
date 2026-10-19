import fs from '@ohos.file.fs';
import type common from '@ohos.app.ability.common';

export class SandboxTransferStore {
  static localExperienceUpload(context: common.Context): string {
    const path = `${context.filesDir}/local-experience-upload.txt`;
    if (!fs.accessSync(path)) {
      const file = fs.openSync(path, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE);
      try { fs.writeSync(file.fd, 'TermHarbor 本机文件传输体验\n'); }
      finally { fs.closeSync(file); }
    }
    return path;
  }
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

import fs from '@ohos.file.fs';
import picker from '@ohos.file.picker';
import type common from '@ohos.app.ability.common';

export class UserFileExportStore {
  static async saveText(context: common.Context, suggestedName: string, text: string): Promise<string> {
    const options = new picker.DocumentSaveOptions();
    options.newFileNames = [UserFileExportStore.safeFileName(suggestedName)];
    const documentPicker = new picker.DocumentViewPicker(context);
    const uris = await documentPicker.save(options);
    if (uris.length === 0) {
      throw new Error('Save canceled');
    }

    const file = fs.openSync(uris[0], fs.OpenMode.READ_WRITE);
    try {
      fs.writeSync(file.fd, text);
    } finally {
      fs.closeSync(file);
    }
    return uris[0];
  }

  private static safeFileName(value: string): string {
    const trimmed = value.trim().length > 0 ? value.trim() : 'session.log';
    const name = trimmed.replace(/[^A-Za-z0-9_.-]/g, '_').slice(0, 64);
    return name.endsWith('.log') ? name : `${name}.log`;
  }
}

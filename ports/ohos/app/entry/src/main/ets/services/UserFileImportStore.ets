import fs from '@ohos.file.fs';
import picker from '@ohos.file.picker';
import type common from '@ohos.app.ability.common';

export class UserFileImportStore {
  static async importIniText(context: common.Context): Promise<string> {
    return UserFileImportStore.importTextFile(context, [
      'Tera Term settings(*.ini)|.ini',
      'All files(*.*)|.*',
    ], 'TERATERM.INI selection canceled');
  }

  static async importMacroText(context: common.Context): Promise<string> {
    return UserFileImportStore.importTextFile(context, [
      'Tera Term macro(*.ttl)|.ttl',
      'Text files(*.txt)|.txt',
      'All files(*.*)|.*',
    ], 'Macro selection canceled');
  }

  static async importTransferFile(context: common.Context): Promise<string> {
    const options = new picker.DocumentSelectOptions();
    options.maxSelectNumber = 1;
    options.fileSuffixFilters = [
      'All files(*.*)|.*',
    ];
    const documentPicker = new picker.DocumentViewPicker(context);
    const uris = await documentPicker.select(options);
    if (uris.length === 0) {
      throw new Error('File selection canceled');
    }

    const sourceUri = uris[0];
    const dir = `${context.filesDir}/ssh_uploads`;
    UserFileImportStore.ensureDirectory(dir);
    const targetPath = `${dir}/${UserFileImportStore.safeFileName(sourceUri)}`;
    try {
      fs.copyFileSync(sourceUri, targetPath);
      return targetPath;
    } catch (error) {
      return sourceUri;
    }
  }

  static async importPrivateKey(context: common.Context): Promise<string> {
    const options = new picker.DocumentSelectOptions();
    options.maxSelectNumber = 1;
    options.fileSuffixFilters = [
      'SSH keys(*.pem,.key,.pub)|.pem,.key,.pub',
      'All files(*.*)|.*',
    ];
    const documentPicker = new picker.DocumentViewPicker(context);
    const uris = await documentPicker.select(options);
    if (uris.length === 0) {
      throw new Error('Private key selection canceled');
    }

    const sourceUri = uris[0];
    const dir = `${context.filesDir}/ssh_keys`;
    UserFileImportStore.ensureDirectory(dir);
    const targetPath = `${dir}/${UserFileImportStore.safeFileName(sourceUri)}`;
    try {
      fs.copyFileSync(sourceUri, targetPath);
      return targetPath;
    } catch (error) {
      return sourceUri;
    }
  }

  private static async importTextFile(context: common.Context,
                                      fileSuffixFilters: string[],
                                      cancelMessage: string): Promise<string> {
    const options = new picker.DocumentSelectOptions();
    options.maxSelectNumber = 1;
    options.fileSuffixFilters = fileSuffixFilters;
    const documentPicker = new picker.DocumentViewPicker(context);
    const uris = await documentPicker.select(options);
    if (uris.length === 0) {
      throw new Error(cancelMessage);
    }

    return fs.readTextSync(uris[0]);
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

  private static safeFileName(uri: string): string {
    const last = uri.split('/').pop() || `id_key_${Date.now()}`;
    const decoded = decodeURIComponent(last);
    return decoded.replace(/[^A-Za-z0-9_.-]/g, '_').slice(0, 64);
  }
}

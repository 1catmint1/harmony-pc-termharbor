import pasteboard from '@ohos.pasteboard';

export class ClipboardService {
  static async copyText(text: string): Promise<void> {
    const data = pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, text);
    const systemPasteboard = pasteboard.getSystemPasteboard();
    await systemPasteboard.setData(data);
  }

  static async readText(): Promise<string> {
    const systemPasteboard = pasteboard.getSystemPasteboard();
    const data = await systemPasteboard.getData();
    return data.getPrimaryText();
  }
}

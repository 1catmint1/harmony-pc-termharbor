import { TerminalNative } from './TerminalNative';

export class SessionLogBuffer {
  private chunks: string[] = [];

  appendFromNative(sessionId: number): number {
    const text = TerminalNative.drainTranscript(sessionId);
    if (text.length === 0) {
      return 0;
    }
    this.chunks.push(text);
    return text.length;
  }

  text(): string {
    return this.chunks.join('');
  }

  clear(): void {
    this.chunks = [];
  }
}

import type { TerminalCell, TerminalColorTheme } from '../model/TerminalTypes';

@Component
export struct TerminalSurface {
  @Prop scrollbackRows: string[];
  @Prop rows: string[];
  @Prop cells: TerminalCell[][];
  @Prop cursorRow: number;
  @Prop cursorCol: number;
  @Prop fontFamily: string;
  @Prop fontSize: number;
  @Prop lineHeight: number;
  @Prop colorTheme: TerminalColorTheme;
  @Prop selectedStartLine: number;
  @Prop selectedEndLine: number;
  @Prop selectedStartCol: number;
  @Prop selectedEndCol: number;

  onLineClick: (lineIndex: number) => void = () => {};
  onLineSelectStart: (lineIndex: number, colIndex: number) => void = () => {};
  onLineSelectUpdate: (lineIndex: number, colIndex: number) => void = () => {};
  onKeyboardInput: (event: KeyEvent) => boolean = () => false;
  onViewportResize: (rows: number, cols: number) => void = () => {};

  private touchStartLine: number = -1;
  private touchStartCol: number = -1;
  private touchStartY: number = 0;
  private touchStartX: number = 0;
  private touchSelecting: boolean = false;

  private terminalColor(index: number): string {
    const palette: string[] = this.palette();
    if (index < 0 || index >= palette.length) {
      return palette[7];
    }
    return palette[index];
  }

  private palette(): string[] {
    if (this.colorTheme === 'light') {
      return [
        '#FFFFFF', '#B3261E', '#137333', '#996F00',
        '#0B57D0', '#7B1FA2', '#00796B', '#202124',
        '#5F6368', '#D93025', '#188038', '#B06000',
        '#1A73E8', '#9C27B0', '#00897B', '#000000',
      ];
    }
    if (this.colorTheme === 'classic') {
      return [
        '#000000', '#D24B4B', '#33FF66', '#E6E65C',
        '#4DA3FF', '#D173FF', '#55FFFF', '#33FF66',
        '#5F5F5F', '#FF6B6B', '#B6FF8A', '#FFFF7A',
        '#8CCBFF', '#E0A4FF', '#9BFFFF', '#FFFFFF',
      ];
    }
    return [
      '#101010', '#C23B3B', '#43A047', '#C9A227',
      '#3F78D4', '#8E44AD', '#269C9C', '#D6D6D6',
      '#686868', '#FF6B6B', '#73D13D', '#FFD666',
      '#69A7FF', '#C084FC', '#5EEAD4', '#FFFFFF',
    ];
  }

  private terminalBackground(): string {
    if (this.colorTheme === 'light') {
      return '#F8F9FA';
    }
    return '#111111';
  }

  private scrollbackForeground(): string {
    if (this.colorTheme === 'light') {
      return '#6B7280';
    }
    if (this.colorTheme === 'classic') {
      return '#62B374';
    }
    return '#8C8C8C';
  }

  private foreground(cell: TerminalCell): string {
    if (cell.inverse) {
      return this.terminalColor(cell.background);
    }
    return this.terminalColor(cell.foreground);
  }

  private cursorForeground(cell: TerminalCell): string {
    if (this.colorTheme === 'light') {
      return '#FFFFFF';
    }
    return this.terminalColor(cell.background);
  }

  private cellWidth(): number {
    return Math.max(7, Math.round(this.fontSize * 0.62));
  }

  private lengthToNumber(value: Length): number {
    const parsed = Number.parseFloat(`${value}`);
    return Number.isNaN(parsed) ? 0 : parsed;
  }

  private reportViewportSize(area: Area): void {
    const width = this.lengthToNumber(area.width);
    const height = this.lengthToNumber(area.height);
    if (width <= 0 || height <= 0) {
      return;
    }
    const cols = Math.max(40, Math.min(220, Math.floor((width - 24) / this.cellWidth())));
    const rows = Math.max(8, Math.min(80, Math.floor((height - 20) / this.lineHeight)));
    this.onViewportResize(rows, cols);
  }

  private lineIsSelected(lineIndex: number): boolean {
    if (this.selectedStartLine < 0 || this.selectedEndLine < 0) {
      return false;
    }
    const start = Math.min(this.selectedStartLine, this.selectedEndLine);
    const end = Math.max(this.selectedStartLine, this.selectedEndLine);
    return lineIndex >= start && lineIndex <= end;
  }

  private columnFromX(x: number, columnCount: number): number {
    if (columnCount <= 0) {
      return 0;
    }
    const rawCol = Math.floor(Math.max(0, x) / this.cellWidth());
    return Math.max(0, Math.min(columnCount - 1, rawCol));
  }

  private selectedStartBeforeEnd(): boolean {
    if (this.selectedStartLine < this.selectedEndLine) {
      return true;
    }
    if (this.selectedStartLine > this.selectedEndLine) {
      return false;
    }
    return this.selectedStartCol <= this.selectedEndCol;
  }

  private cellIsSelected(lineIndex: number, colIndex: number): boolean {
    if (this.selectedStartLine < 0 || this.selectedEndLine < 0 ||
        this.selectedStartCol < 0 || this.selectedEndCol < 0) {
      return false;
    }
    const forward = this.selectedStartBeforeEnd();
    const startLine = forward ? this.selectedStartLine : this.selectedEndLine;
    const startCol = forward ? this.selectedStartCol : this.selectedEndCol;
    const endLine = forward ? this.selectedEndLine : this.selectedStartLine;
    const endCol = forward ? this.selectedEndCol : this.selectedStartCol;
    if (lineIndex < startLine || lineIndex > endLine) {
      return false;
    }
    if (startLine === endLine) {
      return colIndex >= startCol && colIndex <= endCol;
    }
    if (lineIndex === startLine) {
      return colIndex >= startCol;
    }
    if (lineIndex === endLine) {
      return colIndex <= endCol;
    }
    return true;
  }

  private rowBackground(lineIndex: number): string {
    if (!this.lineIsSelected(lineIndex)) {
      return this.terminalBackground();
    }
    if (this.colorTheme === 'light') {
      return '#D6E9FF';
    }
    if (this.colorTheme === 'classic') {
      return '#163B20';
    }
    return '#253B55';
  }

  private cellBackground(cell: TerminalCell, lineIndex: number, rowIndex: number, cellIndex: number): string {
    if (this.cellIsSelected(lineIndex, cellIndex)) {
      return this.rowBackground(lineIndex);
    }
    if (lineIndex === this.scrollbackRows.length + this.cursorRow && rowIndex === this.cursorRow &&
        cellIndex === this.cursorCol) {
      return this.colorTheme === 'light' ? '#202124' : '#D6D6D6';
    }
    if (cell.inverse) {
      return this.terminalColor(cell.foreground);
    }
    return this.terminalColor(cell.background);
  }

  private cellForeground(cell: TerminalCell, lineIndex: number, rowIndex: number, cellIndex: number): string {
    if (lineIndex === this.scrollbackRows.length + this.cursorRow && rowIndex === this.cursorRow &&
        cellIndex === this.cursorCol) {
      return this.cursorForeground(cell);
    }
    return this.foreground(cell);
  }

  private totalLineCount(): number {
    return this.scrollbackRows.length + this.rows.length;
  }

  private clampLine(lineIndex: number): number {
    const totalLines = this.totalLineCount();
    if (totalLines <= 0) {
      return -1;
    }
    return Math.max(0, Math.min(totalLines - 1, lineIndex));
  }

  private focusTerminal(): void {
    focusControl.requestFocus('terminal-surface');
  }

  private updateTouchSelection(event: TouchEvent, startLineIndex: number, columnCount: number): void {
    if (event.type === TouchType.Down) {
      this.focusTerminal();
      if (event.touches.length === 0) {
        return;
      }
      const lineIndex = this.clampLine(startLineIndex);
      if (lineIndex < 0) {
        return;
      }
      this.touchStartLine = lineIndex;
      this.touchStartCol = this.columnFromX(event.touches[0].x, columnCount);
      this.touchStartY = event.touches[0].y;
      this.touchStartX = event.touches[0].x;
      this.touchSelecting = false;
      return;
    }
    if (event.type === TouchType.Move) {
      if (event.touches.length === 0 || this.touchStartLine < 0) {
        return;
      }
      const deltaY = event.touches[0].y - this.touchStartY;
      const deltaX = event.touches[0].x - this.touchStartX;
      if (!this.touchSelecting && Math.abs(deltaY) < this.lineHeight / 2 &&
          Math.abs(deltaX) < this.cellWidth()) {
        return;
      }
      if (!this.touchSelecting) {
        this.onLineSelectStart(this.touchStartLine, this.touchStartCol);
        this.touchSelecting = true;
      }
      const deltaLines = Math.round(deltaY / this.lineHeight);
      const lineIndex = this.clampLine(this.touchStartLine + deltaLines);
      if (lineIndex >= 0) {
        this.onLineSelectUpdate(lineIndex, this.columnFromX(event.touches[0].x, columnCount));
      }
      return;
    }
    if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
      this.touchStartLine = -1;
      this.touchStartCol = -1;
      this.touchSelecting = false;
    }
  }

  build() {
    Scroll() {
      Column() {
        ForEach(this.scrollbackRows, (row: string, index: number) => {
          Text(row)
            .fontFamily(this.fontFamily)
            .fontSize(this.fontSize)
            .fontColor(this.scrollbackForeground())
            .width('100%')
            .height(this.lineHeight)
            .maxLines(1)
            .textOverflow({ overflow: TextOverflow.Clip })
            .backgroundColor(this.rowBackground(index))
            .copyOption(CopyOptions.InApp)
            .onTouch((event: TouchEvent) => this.updateTouchSelection(event, index, row.length))
            .onClick(() => {
              this.focusTerminal();
              this.onLineClick(index);
            })
        }, (row: string, index: number) => `scrollback:${index}:${row}`)

        ForEach(this.cells, (row: TerminalCell[], index: number) => {
          Row() {
            ForEach(row, (cell: TerminalCell, cellIndex: number) => {
              Text(cell.text)
                .fontFamily(this.fontFamily)
                .fontSize(this.fontSize)
                .fontColor(this.cellForeground(cell, this.scrollbackRows.length + index, index, cellIndex))
                .fontWeight(cell.bold ? FontWeight.Bold : FontWeight.Regular)
                .decoration({ type: cell.underline ? TextDecorationType.Underline : TextDecorationType.None })
                .width(this.cellWidth() * (cell.columns === undefined ? 1 : cell.columns))
                .height(this.lineHeight)
                .maxLines(1)
                .textOverflow({ overflow: TextOverflow.Clip })
                .textAlign(TextAlign.Start)
                .backgroundColor(this.cellBackground(cell, this.scrollbackRows.length + index, index, cellIndex))
                .copyOption(CopyOptions.InApp)
            }, (cell: TerminalCell, cellIndex: number) => `${index}:${cellIndex}:${cell.text}:${cell.foreground}:${cell.background}:${cell.bold}:${cell.inverse}`)
          }
            .width('100%')
            .height(this.lineHeight)
            .backgroundColor(this.rowBackground(this.scrollbackRows.length + index))
            .alignItems(VerticalAlign.Center)
            .onTouch((event: TouchEvent) => this.updateTouchSelection(event, this.scrollbackRows.length + index, row.length))
            .onClick(() => {
              this.focusTerminal();
              this.onLineClick(this.scrollbackRows.length + index);
            })
        }, (row: TerminalCell[], index: number) => `${index}:${this.rows[index]}`)
      }
      .alignItems(HorizontalAlign.Start)
      .padding({ left: 12, right: 12, top: 10, bottom: 10 })
      .width('100%')
    }
    .backgroundColor(this.terminalBackground())
    .width('100%')
    .layoutWeight(1)
    .id('terminal-surface')
    .focusable(true)
    .focusOnTouch(true)
    .onAreaChange((_oldArea: Area, newArea: Area): void => this.reportViewportSize(newArea))
    .onKeyEvent((event: KeyEvent): boolean => this.onKeyboardInput(event))
  }
}

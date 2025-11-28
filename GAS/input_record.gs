// --- 設定 ---
const FOLDER_ID = '1ou0BsBw88D4tzNmwaIfRu1twWLt-p8N8'.trim(); 

// 手動入力項目のヘッダー定義
const MANUAL_HEADERS = ['Penalty(sec)', 'CourseOK', 'Note']; 

function importLatestCSV() {
  console.log("処理開始...");

  // 1. フォルダ取得
  let folder;
  try {
    folder = DriveApp.getFolderById(FOLDER_ID);
  } catch (e) {
    console.error("フォルダが見つかりません: " + e.message);
    return;
  }

  // 2. 今日のファイル検索
  const now = new Date();
  const fileDateStr = Utilities.formatDate(now, Session.getScriptTimeZone(), 'yyyyMMdd');
  const displayDateStr = Utilities.formatDate(now, Session.getScriptTimeZone(), 'yyyy-MM-dd');
  
  const fileName = 'gymkhana_' + fileDateStr + '.csv';
  const files = folder.getFilesByName(fileName);
  
  if (!files.hasNext()) {
    console.log("本日のCSVなし: " + fileName);
    return;
  }
  
  // 3. CSV読み込み
  const file = files.next();
  const csvData = Utilities.parseCsv(file.getBlob().getDataAsString());
  
  if (csvData.length <= 1) return;

  const ss = SpreadsheetApp.getActiveSpreadsheet();
  
  // ヘッダー結合
  const csvHeader = csvData[0];
  const fullHeader = csvHeader.concat(MANUAL_HEADERS);

  // 4. モード別に振り分け
  const rowsByMode = {}; 
  for (let i = 1; i < csvData.length; i++) {
    const row = csvData[i];
    const mode = (row.length > 7) ? row[7].toUpperCase() : "UNKNOWN";
    if (!rowsByMode[mode]) rowsByMode[mode] = [];
    rowsByMode[mode].push(row);
  }

  // 5. 書き込み実行
  for (const mode in rowsByMode) {
    const targetRows = rowsByMode[mode];
    if (targetRows.length === 0) continue;

    const sheetName = displayDateStr + "_" + mode;
    let sheet = ss.getSheetByName(sheetName);
    
    // シート作成 & ヘッダー設定
    if (!sheet) {
      sheet = ss.insertSheet(sheetName, 0);
      sheet.appendRow(fullHeader);
      sheet.getRange(1, 1, 1, fullHeader.length).setFontWeight("bold").setBackground("#efefef");
      sheet.setFrozenRows(1);
      console.log("新規シート作成: " + sheetName);
    }

    writeUniqueRows(sheet, targetRows);
  }
}

// --- 重複チェックして書き込み & 入力規則設定 ---
function writeUniqueRows(sheet, newRows) {
  const lastRow = sheet.getLastRow();
  const existingKeys = new Set();
  const csvColCount = newRows[0].length; 

  // 既存データのキー取得
  if (lastRow > 1) {
    const sheetValues = sheet.getRange(2, 1, lastRow - 1, csvColCount).getValues();
    for (let i = 0; i < sheetValues.length; i++) {
      existingKeys.add(generateKey(sheetValues[i]));
    }
  }

  const rowsToAdd = [];
  for (let i = 0; i < newRows.length; i++) {
    const row = newRows[i];
    const key = generateKey(row);
    
    if (!existingKeys.has(key)) {
      // CSVデータ + 手動入力の初期値
      // Penalty(空欄), CourseOK("〇"), Note(空欄)
      const extendedRow = row.concat(["", "〇", ""]);
      rowsToAdd.push(extendedRow);
      existingKeys.add(key);
    }
  }

  if (rowsToAdd.length > 0) {
    // 書き込み開始行
    const startRow = lastRow + 1;
    const numRows = rowsToAdd.length;
    const numCols = rowsToAdd[0].length;

    // 1. データを書き込む
    const range = sheet.getRange(startRow, 1, numRows, numCols);
    range.setValues(rowsToAdd);

    // 2. 入力規則を設定する (CourseOK列 = 右から2番目)
    // 列番号を計算 (CSV列数 + 2番目の手動項目)
    const courseOkColIndex = csvColCount + 2; 
    const ruleRange = sheet.getRange(startRow, courseOkColIndex, numRows, 1);
    
    // プルダウン作成 (〇 / ×)
    const rule = SpreadsheetApp.newDataValidation()
      .requireValueInList(['〇', '×'], true)
      .setAllowInvalid(false)
      .build();
    ruleRange.setDataValidation(rule);

    // 3. 書式設定 (オプション: 中央揃えなど)
    ruleRange.setHorizontalAlignment("center");

    console.log(sheet.getName() + " に " + numRows + " 件追加");
  }
}

// --- キー生成 ---
function generateKey(row) {
  const timestamp = normalizeTime(row[0]);
  const id = normalize(row[2]);
  const time = normalize(row[4]);
  return timestamp + "_" + id + "_" + time;
}

function normalize(val) {
  if (val === null || val === undefined) return "";
  return String(val).trim();
}

function normalizeTime(val) {
  if (!val) return "";
  if (val instanceof Date) {
    return Utilities.formatDate(val, Session.getScriptTimeZone(), "HH:mm:ss");
  }
  return String(val).trim();
}

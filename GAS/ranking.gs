/**
 * 現在開いているシートのデータを元に、総合順位を作成する
 * (最新フォーマット対応: Vehicle, Status, Penalty, CourseOK)
 */
function updateRanking() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  
  // 今見ているシートを元データにする
  const rawSheet = ss.getActiveSheet();
  const rawSheetName = rawSheet.getName();

  // エラー回避: 既に順位シートや別のシートを開いている場合
  if (rawSheetName.includes("_Ranking")) {
    Browser.msgBox("エラー: 計測データのシート(日付のシート)を開いてから実行してください。");
    return;
  }

  // 順位シートの名前 (例: "2025-11-23_NORMAL_Ranking")
  const RANK_SHEET_NAME = rawSheetName + "_Ranking"; 
  let rankSheet = ss.getSheetByName(RANK_SHEET_NAME);

  // 順位シートがなければ作成
  if (!rankSheet) {
    rankSheet = ss.insertSheet(RANK_SHEET_NAME, rawSheet.getIndex() + 1);
  }

  // 1. 生データの取得
  const lastRow = rawSheet.getLastRow();
  if (lastRow < 2) {
    Browser.msgBox("データがありません。");
    return;
  }
  
  // A列(1) 〜 K列(11) まで取得
  // [0]Timestamp, [1]Name, [2]ID, [3]Vehicle, [4]Time, [5]RT, [6]Status, [7]Mode, [8]Penalty, [9]CourseOK, [10]Note
  const data = rawSheet.getRange(2, 1, lastRow - 1, 11).getValues();

  // 2. ベストタイム集計
  // Key: ID, Value: {riderObj}
  let riders = {};

  data.forEach(row => {
    const name = row[1];     // RiderName
    const id = row[2];       // ID
    const vehicle = row[3];  // Vehicle
    
    // タイムとペナルティを数値化
    const rawTime = parseFloat(row[4]);
    const penaltySec = parseFloat(row[8]) || 0; // I列: Penalty(sec)
    
    // ミスコース判定 (J列: CourseOK が "×" なら無効)
    const courseStatus = row[9];
    const isMissCourse = (courseStatus === "×" || courseStatus === "X"); 

    // 記録なし条件 (タイムが無効、またはミスコース)
    if (isNaN(rawTime) || isMissCourse) return;

    // ★公式タイム = 生タイム + ペナルティ秒数
    const officialTime = rawTime + penaltySec;

    // まだ登録がない、または今回のタイムの方が速い(更新した)場合
    if (!riders[id] || officialTime < riders[id].officialTime) {
      riders[id] = {
        name: name,
        vehicle: vehicle,
        id: id,
        officialTime: officialTime,
        rawTime: rawTime,
        penalty: penaltySec,
        status: row[6] // Status (OK / FALSE START)
      };
    }
  });

  // 3. リスト化してソート (タイムが速い順)
  let rankingList = Object.values(riders);
  rankingList.sort((a, b) => a.officialTime - b.officialTime);

  // 4. 順位表データの作成
  let topTime = rankingList.length > 0 ? rankingList[0].officialTime : 0;
  
  let outputData = rankingList.map((r, index) => {
    // トップ比 (%)
    let percentage = (r.officialTime / topTime) * 100;
    
    // フライングなどのステータス注記
    let note = "";
    if (r.status.includes("FALSE")) note = "(F)";
    
    // 表示データの作成
    return [
      index + 1,                 // 順位
      r.name,                    // 名前
      r.vehicle,                 // 車両
      r.officialTime.toFixed(3), // ★公式記録
      percentage.toFixed(2) + "%", // 比率
      r.rawTime.toFixed(3),      // (参考) 生タイム
      (r.penalty > 0 ? "+" + r.penalty : ""), // (参考) ペナルティ
      note                       // (参考) 注記
    ];
  });

  // 5. 結果シートへの書き込み
  rankSheet.clear(); 
  
  // ヘッダー
  const headers = [["Rank", "Rider", "Vehicle", "Time", "Top%", "(Raw)", "(Pen)", "Note"]];
  rankSheet.getRange(1, 1, 1, headers[0].length).setValues(headers);
  
  // デザイン調整
  rankSheet.getRange("A1:H1").setFontWeight("bold").setBackground("#4a86e8").setFontColor("white");
  rankSheet.setFrozenRows(1);

  if (outputData.length > 0) {
    // データ書き込み
    rankSheet.getRange(2, 1, outputData.length, outputData[0].length).setValues(outputData);
    
    // 1位: 金, 2位: 銀, 3位: 銅 の色付け
    if (outputData.length >= 1) rankSheet.getRange("A2:H2").setBackground("#fff2cc"); // Gold
    if (outputData.length >= 2) rankSheet.getRange("A3:H3").setBackground("#efefef"); // Silver
    if (outputData.length >= 3) rankSheet.getRange("A4:H4").setBackground("#f4cccc"); // Bronze
  }
  
  // 列幅自動調整
  rankSheet.autoResizeColumns(1, 8);

  // 完了通知
  ss.toast("集計完了！ トップ: " + topTime.toFixed(3), "Ranking Updated");
}

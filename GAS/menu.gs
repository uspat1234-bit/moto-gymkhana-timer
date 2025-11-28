/**
 * スプレッドシートを開いたときにメニューを追加する
 */
function onOpen() {
  const ui = SpreadsheetApp.getUi();
  ui.createMenu('⚡ ジムカーナ機能')
    .addItem('📥 最新CSVを取り込む', 'importLatestCSV') // ★これを追加
    .addSeparator() // 区切り線を入れると見やすいです
    .addItem('🏆 順位表を更新', 'updateRanking')
    .addToUi();
}

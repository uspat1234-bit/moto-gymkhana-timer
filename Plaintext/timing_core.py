import time
import threading
import random
import gpio_sensor
import os
import csv
import datetime

class TimingSystem:
    def __init__(self):
        self.data_lock = threading.Lock()
        self.running = True
        
        # --- 設定値 ---
        self.SENSOR_COOLDOWN = 3.0     # センサーの不感時間 (秒)
        self.NEXT_START_INTERVAL = 5.0 # 次の走者がスタートできるまでの間隔 (秒)
        self.GOAL_DISPLAY_TIME = 5.0   # ゴール後の表示時間 (秒)
        
        # データ保存先 (環境に合わせて変更してください)
        # ノートPCなら "C:/gymkhana_data" などでもOK
        # ラズパイなら "/home/ori/gymkhana_data"
        self.DATA_DIR = "gymkhana_data" 
        self.ensure_data_dir()
        
        # --- 状態管理 ---
        self.queue = []             # 待機列 (Entry)
        self.on_course_runners = [] # コース上の走者リスト (Running)
        self.current_runner = None  # 画面表示のメイン走者
        
        self.elapsed_time = 0.0
        self.goal_hold_expire_time = None
        
        # センサー管理
        self.last_start_trigger_time = 0
        self.last_stop_trigger_time = 0
        
        # ステータス表示テキスト
        self.d1_status_text = "START READY"
        self.d2_status_text = "STOP READY"
        
        # --- モード・継承クラス用初期値 ---
        self.mode = "NORMAL"
        self.signal_stage = "IDLE"
        self.signal_start_time = 0.0
        self.reaction_time = None
        self.false_start = False
        self.PRE_STAGE_WAIT = 2.0
        self.STAGE_WAIT_MIN = 1.0
        self.STAGE_WAIT_MAX = 2.5
        
        self.nfc_ready = True

    def ensure_data_dir(self):
        """データ保存フォルダを作成"""
        if not os.path.exists(self.DATA_DIR):
            os.makedirs(self.DATA_DIR)
            try:
                # ラズパイなどで権限エラーが出ないように緩める
                os.chmod(self.DATA_DIR, 0o777)
            except:
                pass

    def save_record(self, runner):
        """計測結果をCSVに保存"""
        try:
            # ファイル名: gymkhana_YYYYMMDD.csv
            today_str = datetime.date.today().strftime("%Y%m%d")
            filename = f"{self.DATA_DIR}/gymkhana_{today_str}.csv"
            
            file_exists = os.path.isfile(filename)
            
            with open(filename, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                # ヘッダー作成 (初回のみ)
                if not file_exists:
                    writer.writerow(['Timestamp', 'RiderName', 'ID', 'Vehicle', 'Time', 'ReactionTime', 'Status', 'Mode'])
                
                timestamp = datetime.datetime.now().strftime("%H:%M:%S")
                rt = f"{runner.get('reaction_time', 0):.3f}" if runner.get('reaction_time') else ""
                status = "FALSE START" if runner.get('false_start') else "OK"
                bike = runner.get('bike', '')
                
                writer.writerow([
                    timestamp,
                    runner['name'],
                    runner['id'],
                    bike,
                    f"{runner['result_time']:.3f}",
                    rt,
                    status,
                    self.mode
                ])
                print(f"💾 記録保存完了: {runner['name']} ({runner['result_time']:.3f}s)")
                
        except Exception as e:
            print(f"❌ 保存エラー: {e}")

    def is_nfc_allowed(self):
        """NFC読み取り許可状態を返す"""
        return self.nfc_ready

    def register_new_rider(self, name, rider_id, bike=""):
        """新しいライダーを登録"""
        with self.data_lock:
            # 重複チェック (待機列の最後尾と同じなら弾く)
            if self.queue and self.queue[-1]['id'] == rider_id:
                print(f"重複エントリー (Queue): {name}")
                return False
            
            # 走行中の人と同じなら弾く (追走での誤反応防止)
            for r in self.on_course_runners:
                if r['id'] == rider_id:
                    print(f"重複エントリー (Running): {name}")
                    return False
            
            new_rider = {
                'name': name,
                'id': rider_id,
                'bike': bike,
                'status': 'WAITING',
                'start_time': None,
                'goal_time': None,
                'result_time': None,
                'reaction_time': None,
                'false_start': False
            }
            self.queue.append(new_rider)
            print(f"登録: {name} (ID:{rider_id}, Bike:{bike})")
            return True

    # Signal用のプレースホルダ (継承先でオーバーライド)
    def start_signal_sequence(self, force=False):
        pass

    def run_sensing_loop(self):
        """センサー監視ループ (NORMALモード・追走対応)"""
        print("センサー監視開始 (NORMAL)")
        
        while self.running:
            current_time = time.time()
            
            # センサー状態取得
            is_start_active = gpio_sensor.is_start_sensor_active()
            is_stop_active = gpio_sensor.is_stop_sensor_active()

            with self.data_lock:
                # --- 0. ステータス表示更新 ---
                # スタート間隔チェック
                if (current_time - self.last_start_trigger_time) < self.NEXT_START_INTERVAL and self.on_course_runners:
                     rem = int(self.NEXT_START_INTERVAL - (current_time - self.last_start_trigger_time))
                     self.d1_status_text = f"WAIT ({rem}s)"
                     is_start_active = False # 間隔内なら強制無効
                elif is_start_active:
                    self.d1_status_text = "ACTIVE!"
                else:
                    self.d1_status_text = "READY"

                # ゴールクールダウンチェック
                if (current_time - self.last_stop_trigger_time) < self.SENSOR_COOLDOWN:
                    is_stop_active = False
                    self.d2_status_text = "COOLDOWN"
                elif is_stop_active:
                    self.d2_status_text = "ACTIVE!"
                else:
                    self.d2_status_text = "READY"

                # --- 1. ゴール表示のリセット ---
                # 表示中のランナーがゴール済みで、表示時間が過ぎたらクリア
                if self.current_runner and self.current_runner['status'] == 'GOAL':
                    if self.goal_hold_expire_time and current_time > self.goal_hold_expire_time:
                        # まだ走っている人がいれば、その人に表示を切り替える
                        if self.on_course_runners:
                            self.current_runner = self.on_course_runners[-1] # 最新の走者
                        else:
                            self.current_runner = None
                        self.goal_hold_expire_time = None

                # --- 2. スタート処理 (追走OK) ---
                # スタートセンサー反応 & 待機者がいる
                if is_start_active and self.queue:
                    self.last_start_trigger_time = current_time
                    
                    # キューから取り出し
                    runner = self.queue.pop(0)
                    runner['status'] = 'RUNNING'
                    runner['start_time'] = current_time
                    
                    # コース上リストに追加
                    self.on_course_runners.append(runner)
                    
                    # 画面表示を「今スタートした人」に切り替える
                    self.current_runner = runner
                    
                    print(f"★スタート: {runner['name']}")

                # --- 3. ゴール処理 (FIFO) ---
                # ゴールセンサー反応 & コース上に誰かいる
                if is_stop_active and self.on_course_runners:
                    # 一番最初にスタートした人 (リストの先頭) をゴールさせる
                    target_runner = self.on_course_runners[0]
                    
                    # 安全策: スタートから3秒未満のゴールは無視
                    if current_time - target_runner['start_time'] > 3.0:
                        self.last_stop_trigger_time = current_time
                        
                        # リストから取り出してゴール確定
                        runner = self.on_course_runners.pop(0)
                        runner['status'] = 'GOAL'
                        runner['result_time'] = current_time - runner['start_time']
                        
                        # 画面表示を「今ゴールした人」に切り替え、5秒維持
                        self.current_runner = runner
                        self.elapsed_time = runner['result_time']
                        self.goal_hold_expire_time = current_time + self.GOAL_DISPLAY_TIME
                        
                        print(f"★ゴール: {runner['result_time']:.3f}")
                        self.save_record(runner)

                # --- 4. タイム更新 ---
                # 画面に表示されている人が「走行中」ならタイムを動かす
                if self.current_runner and self.current_runner['status'] == 'RUNNING':
                    self.elapsed_time = current_time - self.current_runner['start_time']
            
            # CPU負荷軽減
            time.sleep(0.01)

    def stop(self):
        self.running = False

    @property
    def gui_data(self):
        """GUIへ渡すデータセット"""
        if self.current_runner:
            c_name = self.current_runner['name']
            c_id = self.current_runner['id']
            c_bike = self.current_runner.get('bike', '')
            is_goal = (self.current_runner['status'] == 'GOAL')
        else:
            c_name = "---"
            c_id = ""
            c_bike = ""
            is_goal = False
            
        queue_list = []
        for r in self.queue:
            queue_list.append({
                'name': r['name'], 
                'id': r['id'], 
                'bike': r.get('bike', '')
            })

        return {
            "current_runner_name": c_name,
            "current_runner_id": c_id,
            "current_runner_bike": c_bike,
            "first_run_elapsed": f"{self.elapsed_time:.3f}",
            "is_goal": is_goal,
            "queue_list": queue_list,
            "queue_size": len(self.queue),
            "d1_status": self.d1_status_text,
            "d2_status": self.d2_status_text,
            # 以下はシグナルモード用のプレースホルダ
            "signal_stage": self.signal_stage,
            "reaction_time": "---",
            "is_false": False
        }
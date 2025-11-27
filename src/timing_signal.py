import time
import threading
import random
import gpio_sensor
from timing_core import TimingSystem

class SignalTimingSystem(TimingSystem):
    def __init__(self):
        super().__init__() # 親クラス(TimingSystem)の初期化
        
        # --- シグナル専用の状態変数 ---
        self.signal_stage = "IDLE" # IDLE, RED, YELLOW, GREEN, FALSE
        self.signal_start_time = 0.0
        self.reaction_time = None
        self.false_start = False
        
        # --- シグナル設定 (秒) ---
        self.PRE_STAGE_WAIT = 2.0  # 赤点灯時間
        self.STAGE_WAIT_MIN = 1.0  # 黄色点灯時間の最小
        self.STAGE_WAIT_MAX = 2.5  # 黄色点灯時間の最大
        
        # --- 追走・インターバル管理 ---
        self.last_valid_start_time = 0.0
        self.NEXT_START_INTERVAL = 10.0 # 次の走者がスタートできるまでの間隔

    def start_signal_sequence(self, force=False):
        """シグナルシーケンスを開始 (別スレッドで実行推奨)"""
        current_time = time.time()
        
        # 待機者がいない場合は開始しない
        if not self.queue:
            print("エラー: 待機者がいません")
            return

        # 強制開始フラグ(force)がない場合、インターバル時間をチェック
        if not force:
            if (current_time - self.last_valid_start_time) < self.NEXT_START_INTERVAL:
                # コース上に既に誰かいる場合のみ制限をかける (初回は制限なし)
                if len(self.on_course_runners) > 0:
                    rem = int(self.NEXT_START_INTERVAL - (current_time - self.last_valid_start_time))
                    print(f"スタート不可: インターバル調整中 (あと{rem}秒)")
                    return

        # 初期化
        self.false_start = False
        self.reaction_time = None

        # 1. RED (セット)
        with self.data_lock:
            self.signal_stage = "RED"
        time.sleep(self.PRE_STAGE_WAIT)

        # 2. YELLOW (用意)
        with self.data_lock:
            # 待機中にフライングしてしまった場合は中断
            if self.signal_stage == "FALSE": return
            self.signal_stage = "YELLOW"
        
        # ランダム時間待機
        wait_time = random.uniform(self.STAGE_WAIT_MIN, self.STAGE_WAIT_MAX)
        start_wait = time.time()
        while (time.time() - start_wait) < wait_time:
            # 待機中にフライングしたら中断
            if self.signal_stage == "FALSE": return
            time.sleep(0.05)

        # 3. GREEN (スタート！)
        with self.data_lock:
            if self.signal_stage == "FALSE": return
            self.signal_stage = "GREEN"
            self.signal_start_time = time.time() # 計測基準点 (0.000秒)

    def run_sensing_loop(self):
        """シグナルモード専用のセンサー監視ループ"""
        print("センサー監視開始 (SIGNALモード)")
        
        while self.running:
            current_time = time.time()
            is_start_active = gpio_sensor.is_start_sensor_active()
            is_stop_active = gpio_sensor.is_stop_sensor_active()

            with self.data_lock:
                # --- A. ステータス表示更新 ---
                # インターバル中はWAIT表示 (ただしシグナル進行中はREADY扱いにしないと違和感あるので調整)
                if (current_time - self.last_valid_start_time) < self.NEXT_START_INTERVAL and self.on_course_runners:
                    if self.signal_stage == "IDLE":
                        rem = int(self.NEXT_START_INTERVAL - (current_time - self.last_valid_start_time))
                        self.d1_status_text = f"WAIT ({rem}s)"
                        is_start_active = False # 強制的にセンサー無効化
                    else:
                        self.d1_status_text = "READY (SIG)"
                elif is_start_active: 
                    self.d1_status_text = "ACTIVE!"
                else: 
                    self.d1_status_text = "READY"

                if (current_time - self.last_stop_trigger_time) < self.SENSOR_COOLDOWN:
                    is_stop_active = False
                    self.d2_status_text = "COOLDOWN"
                elif is_stop_active: 
                    self.d2_status_text = "ACTIVE!"
                else: 
                    self.d2_status_text = "READY"

                # --- B. ゴール表示のリセット ---
                if self.current_runner and self.current_runner['status'] in ['GOAL', 'FALSE']:
                    if self.goal_hold_expire_time and current_time > self.goal_hold_expire_time:
                        # まだ走っている人がいれば切り替え
                        if self.on_course_runners:
                            self.current_runner = self.on_course_runners[-1]
                        else:
                            self.current_runner = None
                        
                        self.start_time = None
                        self.elapsed_time = 0.0
                        self.goal_hold_expire_time = None
                        self.signal_stage = "IDLE"
                        self.reaction_time = None

                # --- C. スタート判定 ---
                if is_start_active and self.queue:
                    
                    # パターン1: フライングスタート (赤・黄の時にセンサー反応)
                    if self.signal_stage in ["RED", "YELLOW"]:
                        self.signal_stage = "FALSE"
                        
                        runner = self.queue.pop(0)
                        runner['status'] = 'RUNNING' # 計測は開始する
                        # フライング時は「通過した瞬間」をスタート時刻とする
                        runner['start_time'] = current_time 
                        runner['false_start'] = True
                        
                        self.on_course_runners.append(runner)
                        self.current_runner = runner
                        
                        # 画面表示用の基準も更新
                        self.start_time = current_time 
                        
                        self.last_valid_start_time = current_time
                        print(f"★フライングスタート: {runner['name']}")

                    # パターン2: 正常スタート (緑の時にセンサー反応)
                    elif self.signal_stage == "GREEN":
                        # リアクションタイム計算 (通過時刻 - 緑点灯時刻)
                        rt = current_time - self.signal_start_time
                        self.reaction_time = rt
                        
                        runner = self.queue.pop(0)
                        runner['status'] = 'RUNNING'
                        
                        # ★重要: 正常時は「緑点灯時刻」を0秒として計測開始
                        runner['start_time'] = self.signal_start_time 
                        runner['reaction_time'] = rt
                        runner['false_start'] = False
                        
                        self.on_course_runners.append(runner)
                        self.current_runner = runner
                        
                        # 画面表示用の基準も緑点灯時刻に
                        self.start_time = self.signal_start_time
                        
                        self.last_valid_start_time = current_time
                        self.signal_stage = "IDLE"
                        print(f"★正常スタート: {runner['name']} (RT:{rt:.3f})")

                # --- D. ゴール判定 ---
                if is_stop_active and self.on_course_runners:
                    # 先頭(一番長く走っている人)をゴール対象とする
                    target_runner = self.on_course_runners[0]
                    
                    # 安全策: スタートから3秒未満のゴールは無視
                    # (Green基準だと start_time が古いので差は大きくなる。問題なし)
                    if current_time - target_runner['start_time'] > 3.0:
                        self.last_stop_trigger_time = current_time
                        
                        runner = self.on_course_runners.pop(0)
                        runner['status'] = 'GOAL'
                        # タイム確定
                        runner['result_time'] = current_time - runner['start_time']
                        
                        if runner.get('false_start'): 
                            runner['status'] = 'FALSE' # 記録上のステータス

                        self.current_runner = runner
                        self.elapsed_time = runner['result_time']
                        self.goal_hold_expire_time = current_time + self.GOAL_DISPLAY_TIME
                        
                        print(f"★ゴール: {runner['result_time']:.3f}")
                        self.save_record(runner)

                # --- E. タイム更新 ---
                if self.current_runner and self.current_runner['status'] == 'RUNNING':
                    self.elapsed_time = current_time - self.current_runner['start_time']

            time.sleep(0.01)

    @property
    def gui_data(self):
        """GUIへ渡すデータ (Signal専用情報追加)"""
        base_data = super().gui_data
        
        # 追加情報
        base_data["signal_stage"] = self.signal_stage
        
        if self.reaction_time:
            base_data["reaction_time"] = f"{self.reaction_time:.3f}"
        else:
            base_data["reaction_time"] = "---"
            
        if self.current_runner and self.current_runner.get('false_start'):
            base_data["is_false"] = True
        else:
            base_data["is_false"] = False
            
        return base_data
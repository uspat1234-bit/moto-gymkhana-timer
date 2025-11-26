import serial
import time
import threading
import sys

# ★設定: ポート番号 (環境に合わせて変更してください)
ARDUINO_PORT = "COM3" 
BAUD_RATE = 9600

# センサーの状態変数
sensor_status = {
    "start": False,
    "stop": False
}

ser = None
thread_running = True

def serial_listener():
    """Arduino監視スレッド"""
    global ser
    print(f"--- センサー監視開始: {ARDUINO_PORT} ---")
    
    while thread_running:
        try:
            # 接続がなければ繋ぐ
            if ser is None or not ser.is_open:
                try:
                    ser = serial.Serial(ARDUINO_PORT, BAUD_RATE, timeout=0.1)
                    print("✅ Arduinoに再接続しました")
                except:
                    time.sleep(1.0) # 接続失敗したら少し待つ
                    continue

            # データ受信
            if ser.in_waiting > 0:
                try:
                    line = ser.readline().decode('utf-8').strip()
                    
                    if line == "START":
                        print("⚡ センサー検知: START") # デバッグ表示
                        sensor_status["start"] = True
                        # 0.2秒後に自動でFalseに戻す
                        threading.Timer(0.2, lambda: reset_status("start")).start()
                    
                    elif line == "STOP":
                        print("⚡ センサー検知: STOP") # デバッグ表示
                        sensor_status["stop"] = True
                        threading.Timer(0.2, lambda: reset_status("stop")).start()
                        
                except Exception as e:
                    print(f"データ解析エラー: {e}")

        except Exception as e:
            print(f"シリアル通信エラー: {e}")
            if ser: ser.close()
            ser = None
            
        time.sleep(0.01)

def reset_status(key):
    sensor_status[key] = False

def setup_gpio():
    """初期化"""
    t = threading.Thread(target=serial_listener, daemon=True)
    t.start()

def is_start_sensor_active():
    return sensor_status["start"]

def is_stop_sensor_active():
    return sensor_status["stop"]

def cleanup_gpio():
    global thread_running
    thread_running = False
    if ser and ser.is_open:
        ser.close()
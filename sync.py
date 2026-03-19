# Clock project
# Copyright (c) 2026 Fedir Vilhota <fredy31415@gmail.com>
# This software is released under the MIT License.
# See the LICENSE file in the project root for full license information.

import serial, time
import sys

# Спрощений скрипт для синхронізації часу
PORT = '/dev/ttyACM0'
BAUD = 115200

def sync():
    try:
        print(f"Відкриття порту {PORT}...")
        # Додаємо write_timeout щоб скрипт не "висів" вічно при помилці запису
        ser = serial.Serial(
            PORT, 
            BAUD, 
            timeout=1, 
            write_timeout=2,
            rtscts=False, 
            dsrdtr=False
        )
        
        print("Очікування ініціалізації пристрою (3 сек)...")
        time.sleep(3)
        
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        timestamp = int(time.time())
        command = f"SET_TIME:{timestamp}\n"
        
        print(f"Надсилання команди: {command.strip()}...")
        ser.write(command.encode())
        ser.flush()
        
        print("Очікування відповіді...")
        start_wait = time.time()
        while time.time() - start_wait < 15:
            if ser.in_waiting:
                line = ser.readline().decode(errors='ignore').strip()
                if line:
                    print(f"ESP32: {line}")
                    if "TIME_SYNC_OK" in line:
                        print("\n✅ УСПІХ: Час синхронізовано!")
                        ser.close()
                        return
        
        print("\n❌ ТАЙМАУТ: Відповідь не отримана.")
        ser.close()
        
    except Exception as e:
        print(f"❌ ПОМИЛКА: {e}")

if __name__ == "__main__":
    sync()

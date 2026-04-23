#  Digital Crowd Mood Estimator v2.0

An IoT semester project that classifies crowd mood and displays it on LCD/LED/Buzzer.

## Modes

1. Microphone mode (on-board): Arduino reads analog mic input and estimates mood.
2. AI hybrid mode (PC + Arduino): Python analyzes video/webcam activity and streams mood to Arduino over serial.

## Hardware (Simulation / Real)

- Arduino Uno
- LCD1602 (I2C)
- LEDs (Green/Yellow/Orange/Red)
- Piezo buzzer
- Microphone sensor (analog)

## Arduino File

- Main sketch: `iot end sem.ino`

## Compile (Arduino CLI)

```powershell
arduino-cli compile --fqbn arduino:avr:uno --libraries . --output-dir build .
```

## Wokwi

`wokwi.toml` expects:

- `build/firmware.hex`
- `build/firmware.elf`

After compile:

```powershell
Copy-Item -LiteralPath ".\\build\\iot end sem.ino.hex" -Destination ".\\build\\firmware.hex" -Force
Copy-Item -LiteralPath ".\\build\\iot end sem.ino.elf" -Destination ".\\build\\firmware.elf" -Force
```

Restart simulation after updating firmware files.

## AI Video-to-Mood Streaming (PC -> Arduino)

### 1) Install Python dependencies

```powershell
pip install -r requirements-video.txt
```

### 2) Upload Arduino sketch and note COM port

- Upload `iot end sem.ino`.
- Ensure serial baud is `115200`.

### 3) Run video analyzer

From video file:

```powershell
python video_mood_stream.py --video "path\\to\\crowd.mp4" --port COM5 --realtime --show
```

From webcam:

```powershell
python video_mood_stream.py --video 0 --port COM5 --show
```

The script sends serial messages like:

`MOOD:EXCITED,SCORE:72`

Arduino uses these messages to update mood output in real time.

## Notes

- If serial messages stop for >3 seconds, Arduino falls back to local microphone mode.
- Press `q` to close preview window in Python script.

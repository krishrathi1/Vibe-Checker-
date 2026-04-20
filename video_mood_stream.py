import argparse
import time
from dataclasses import dataclass

import cv2
import numpy as np

try:
    import serial
except Exception:
    serial = None


@dataclass
class MoodResult:
    mood: str
    score: int
    raw: float
    smooth: float


def classify_mood(score: int) -> str:
    if score < 25:
        return "CALM"
    if score < 50:
        return "ACTIVE"
    if score < 75:
        return "EXCITED"
    return "CHAOTIC"


def compute_raw_activity(gray: np.ndarray, prev_gray: np.ndarray) -> float:
    motion = float(np.mean(cv2.absdiff(gray, prev_gray)))
    contrast = float(np.std(gray))
    edges = cv2.Canny(gray, 80, 160)
    edge_density = float(np.mean(edges > 0) * 100.0)
    return (0.6 * motion) + (0.25 * contrast) + (0.15 * edge_density)


def analyze_stream(
    source: str,
    sample_interval: float,
    realtime: bool,
    show_preview: bool,
):
    cap_source = 0 if source == "0" else source
    cap = cv2.VideoCapture(cap_source)
    if not cap.isOpened():
        raise RuntimeError(f"Unable to open video source: {source}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    if fps <= 1 or np.isnan(fps):
        fps = 30.0

    prev_gray = None
    smoothed = None
    adaptive_max = 20.0
    frame_index = 0
    start_wall = time.time()
    next_emit = 0.0

    while True:
        ok, frame = cap.read()
        if not ok:
            break

        frame_time = frame_index / fps
        frame_index += 1

        if realtime:
            elapsed = time.time() - start_wall
            if frame_time > elapsed:
                time.sleep(frame_time - elapsed)

        if frame_time < next_emit:
            continue
        next_emit = frame_time + sample_interval

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        if prev_gray is None:
            prev_gray = gray
            continue

        raw = compute_raw_activity(gray, prev_gray)
        prev_gray = gray

        if smoothed is None:
            smoothed = raw
        else:
            smoothed = (0.8 * smoothed) + (0.2 * raw)

        adaptive_max = max(12.0, adaptive_max * 0.995, smoothed)
        score = int(np.clip((smoothed / adaptive_max) * 100.0, 0.0, 100.0))
        mood = classify_mood(score)

        if show_preview:
            overlay = frame.copy()
            cv2.putText(
                overlay,
                f"{mood} ({score})",
                (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX,
                1.0,
                (0, 255, 0),
                2,
                cv2.LINE_AA,
            )
            cv2.imshow("Crowd Mood Analyzer", overlay)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

        yield MoodResult(mood=mood, score=score, raw=raw, smooth=smoothed)

    cap.release()
    if show_preview:
        cv2.destroyAllWindows()


def main():
    parser = argparse.ArgumentParser(
        description="Analyze video activity and stream crowd mood to Arduino over serial."
    )
    parser.add_argument("--video", required=True, help="Video file path, or 0 for webcam.")
    parser.add_argument("--port", default="", help="Arduino serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument(
        "--interval",
        type=float,
        default=0.4,
        help="Seconds between mood updates",
    )
    parser.add_argument(
        "--realtime",
        action="store_true",
        help="Replay file in real-time speed (recommended for demo).",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Show analyzed preview window (press q to quit).",
    )
    args = parser.parse_args()

    ser = None
    if args.port:
        if serial is None:
            raise RuntimeError("pyserial is not installed. Install dependencies first.")
        ser = serial.Serial(args.port, args.baud, timeout=1)
        time.sleep(2.0)

    try:
        for result in analyze_stream(
            source=args.video,
            sample_interval=max(0.1, args.interval),
            realtime=args.realtime,
            show_preview=args.show,
        ):
            line = f"MOOD:{result.mood},SCORE:{result.score}\n"
            if ser is not None:
                ser.write(line.encode("utf-8"))
            print(
                f"{line.strip()} | raw={result.raw:.2f} smooth={result.smooth:.2f}",
                flush=True,
            )
    finally:
        if ser is not None:
            ser.close()


if __name__ == "__main__":
    main()


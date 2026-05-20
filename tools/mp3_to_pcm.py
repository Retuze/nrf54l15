"""
Convert MP3 to 16-bit mono PCM C array for nRF54L15 I2S playback.

I2S config: 15.625 kHz, 16-bit left-aligned, left-channel.
PCM stored as uint16_t in flash (2 bytes/sample) to save space.
DMA handler expands to uint32_t at runtime: buf[i] = ((uint32_t)sample) << 16

Usage:
    python mp3_to_pcm.py <input.mp3> [start_sec] [duration_sec] [output_prefix]
"""
import subprocess
import struct
import sys
import os

SAMPLE_RATE = 15625  # 32MHz/8/256 = 15,625 Hz 整除, 无时钟抖动


def mp3_to_raw_pcm(input_path, start_sec, duration_sec):
    """Decode MP3 to raw s16le mono PCM + WAV for preview."""
    raw_path = input_path + ".raw"
    wav_path = input_path + ".preview.wav"
    args = [
        "ffmpeg", "-y", "-v", "error",
        "-ss", str(start_sec),
        "-t", str(duration_sec),
        "-i", input_path,
        "-ar", str(SAMPLE_RATE),
        "-ac", "1",
        "-sample_fmt", "s16",
        "-f", "s16le",
        raw_path,
    ]
    subprocess.run(args, check=True)
    # Also output a WAV for PC preview
    subprocess.run([
        "ffmpeg", "-y", "-v", "error",
        "-ss", str(start_sec),
        "-t", str(duration_sec),
        "-i", input_path,
        "-ar", str(SAMPLE_RATE),
        "-ac", "1",
        "-sample_fmt", "s16",
        wav_path,
    ], check=True)
    return raw_path, wav_path


def raw_to_c_array(raw_path, output_prefix, gain=1.0):
    """Convert raw s16le PCM to C const uint16_t array with optional gain."""
    with open(raw_path, "rb") as f:
        raw = f.read()

    num_samples = len(raw) // 2
    samples = list(struct.unpack(f"<{num_samples}h", raw))

    # Apply gain
    if gain != 1.0:
        samples = [max(-32768, min(32767, int(s * gain))) for s in samples]

    # Store as uint16_t (2 bytes/sample in flash, expand in DMA handler)
    h_path = f"{output_prefix}.h"
    c_path = f"{output_prefix}.c"

    array_name = os.path.basename(output_prefix) + "_pcm"
    total_ms = num_samples * 1000 // SAMPLE_RATE
    total_sec = total_ms / 1000.0

    guard = f"{array_name.upper()}_H"

    header = f"""/* Auto-generated — do not edit. */
#ifndef {guard}
#define {guard}

#include <stdint.h>

#define PCM_SAMPLE_RATE  {SAMPLE_RATE}
#define PCM_NUM_SAMPLES  {num_samples}
#define PCM_DURATION_MS  {total_ms}

extern const uint16_t {array_name}[{num_samples}];

#endif /* {guard} */
"""

    c_source = f"""/* Auto-generated — do not edit. */
#include "{os.path.basename(h_path)}"

const uint16_t {array_name}[{num_samples}] = {{
"""
    # Write 16 values per line
    for i in range(0, len(samples), 16):
        chunk = samples[i : i + 16]
        hex_vals = ", ".join(f"0x{s & 0xFFFF:04X}" for s in chunk)
        if i + 16 < len(samples):
            c_source += f"    {hex_vals},\n"
        else:
            c_source += f"    {hex_vals}\n"

    c_source += "};\n"

    with open(h_path, "w") as f:
        f.write(header)
    with open(c_path, "w") as f:
        f.write(c_source)

    # Also write a WAV with gain applied for PC preview
    wav_path = output_prefix + ".wav"
    _write_wav(wav_path, samples)

    kb = os.path.getsize(raw_path) / 1024.0
    c_file_kb = os.path.getsize(c_path) / 1024.0
    flash_kb = num_samples * 2 / 1024.0
    db = 20.0 * (gain ** 0.5) if gain > 0 else float('-inf')
    print(f"Gain:     {gain:.2f} ({db:.1f} dB)")
    print(f"Output:  {h_path}")
    print(f"         {c_path}")
    print(f"         {wav_path}")
    print(f"Samples: {num_samples}")
    print(f"Duration: {total_sec:.2f} s ({total_ms} ms)")
    print(f"Flash:    {flash_kb:.1f} KB (uint16_t)")
    print(f"C file:   {c_file_kb:.1f} KB")

    os.unlink(raw_path)
    return h_path, c_path


def _write_wav(path, samples):
    """Write a mono 16-bit WAV file."""
    import struct as st
    data = b"".join(st.pack("<h", s) for s in samples)
    header = b"".join([
        b"RIFF",
        st.pack("<I", 36 + len(data)),
        b"WAVE",
        b"fmt ",
        st.pack("<I", 16),       # chunk size
        st.pack("<H", 1),        # PCM
        st.pack("<H", 1),        # mono
        st.pack("<I", SAMPLE_RATE),
        st.pack("<I", SAMPLE_RATE * 2),  # byte rate
        st.pack("<H", 2),        # block align
        st.pack("<H", 16),       # bits per sample
        b"data",
        st.pack("<I", len(data)),
    ])
    with open(path, "wb") as f:
        f.write(header + data)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    input_mp3 = sys.argv[1]
    start = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
    duration = float(sys.argv[3]) if len(sys.argv) > 3 else 35.0
    gain = float(sys.argv[4]) if len(sys.argv) > 4 else 0.5
    output_prefix = sys.argv[5] if len(sys.argv) > 5 else "pcm_audio"

    if not os.path.exists(input_mp3):
        print(f"File not found: {input_mp3}")
        sys.exit(1)

    print(f"Input:    {input_mp3}")
    print(f"Start:    {start}s")
    print(f"Duration: {duration}s")
    print(f"Rate:     {SAMPLE_RATE} Hz mono s16")

    raw, wav = mp3_to_raw_pcm(input_mp3, start, duration)
    print(f"WAV:     {wav}")
    raw_to_c_array(raw, output_prefix, gain=gain)

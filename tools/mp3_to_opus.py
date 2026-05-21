"""
Convert MP3 to embedded Opus C array for nRF54L15 firmware.

Wire format (read by opus_player.c):
  uint16_le packet_count
  for each packet: uint16_le byte_len + byte_len bytes of raw Opus data

Each packet encodes exactly 20ms of audio at SAMPLE_RATE.
Uses FFmpeg (libopus) for encoding, Python to parse Ogg and emit the C array.

Usage:
  python mp3_to_opus.py <input.mp3> [start_sec] [duration_sec] [output_prefix]
"""

import subprocess
import struct
import sys
import os

SAMPLE_RATE   = 16000
FRAME_MS      = 20
SAMPLES_PER_FRAME = SAMPLE_RATE * FRAME_MS // 1000  # 320


def parse_ogg_extract_opus_packets(ogg_path):
    """Parse Ogg Opus file and return list of raw Opus packet bytes.
    Skips ID and comment header pages."""
    with open(ogg_path, "rb") as f:
        data = f.read()

    packets = []
    pos = 0
    page_idx = 0

    while pos < len(data):
        if data[pos:pos + 4] != b"OggS":
            break

        header_type = data[pos + 5]
        num_segs    = data[pos + 26]
        seg_table   = data[pos + 27 : pos + 27 + num_segs]

        seg_start = pos + 27 + num_segs
        for seg_len in seg_table:
            seg_start += seg_len
        page_size = seg_start - pos

        # ID header (page 0) and comment header (page 1) contain no audio
        if page_idx >= 2:
            seg_off = pos + 27 + num_segs
            for seg_len in seg_table:
                if seg_len > 0:
                    pkt = data[seg_off : seg_off + seg_len]
                    packets.append(pkt)
                seg_off += seg_len

        pos += page_size
        page_idx += 1

    return packets


def write_c_array(packets, output_prefix):
    """Write embedded_audio.h in the format opus_player.c expects."""
    h_path = f"{output_prefix}.h"

    total_pkts   = len(packets)
    total_bytes  = sum(len(p) for p in packets) + 2 + total_pkts * 2
    duration_ms  = total_pkts * FRAME_MS
    duration_s   = duration_ms / 1000.0

    guard = "EMBEDDED_AUDIO_H"

    # Header
    header = f"""// Auto-generated embedded Opus audio (raw packets, no Ogg container).
// {total_pkts} packets, {SAMPLE_RATE} Hz mono, {FRAME_MS}ms frames
// Duration: {duration_s:.1f}s, wire bytes: {total_bytes}
#ifndef {guard}
#define {guard}

#include <stdint.h>
#include <stddef.h>

#define EMBEDDED_AUDIO_SAMPLE_RATE {SAMPLE_RATE}
#define EMBEDDED_AUDIO_PACKET_COUNT {total_pkts}

// Format: uint16_le packet_count, then for each: uint16_le len + raw Opus data
static const uint8_t embedded_opus_data[] = {{
"""

    # Build data: first uint16_le packet_count
    out = struct.pack("<H", total_pkts)
    for p in packets:
        out += struct.pack("<H", len(p)) + p

    # Hex dump, 16 bytes per line
    lines = []
    for i in range(0, len(out), 16):
        chunk = out[i : i + 16]
        hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
        if i + 16 < len(out):
            lines.append(f"    {hex_str},")
        else:
            lines.append(f"    {hex_str}")

    body = "\n".join(lines)
    footer = "\n};\n\n#endif /* EMBEDDED_AUDIO_H */\n"

    with open(h_path, "w", encoding="utf-8") as f:
        f.write(header + body + footer)

    kb_wire = total_bytes / 1024.0
    kb_file = os.path.getsize(h_path) / 1024.0
    pcm_kb  = duration_ms * SAMPLE_RATE // 1000 * 2 / 1024.0

    print(f"Packets:   {total_pkts}")
    print(f"Duration:  {duration_s:.1f}s ({duration_ms}ms)")
    print(f"Wire:      {total_bytes} bytes ({kb_wire:.1f} KB)")
    print(f"C file:    {kb_file:.1f} KB")
    print(f"vs PCM:    {pcm_kb:.0f} KB ({pcm_kb / max(kb_wire, 1):.1f}x larger)")
    print(f"Output:    {h_path}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    input_mp3   = sys.argv[1]
    start       = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
    duration    = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0
    output_prefix = sys.argv[4] if len(sys.argv) > 4 else "embedded_audio"

    if not os.path.exists(input_mp3):
        print(f"File not found: {input_mp3}")
        sys.exit(1)

    duration_str = str(duration)

    print(f"Input:     {input_mp3}")
    print(f"Start:     {start}s")
    print(f"Duration:  {duration_str}s")
    print(f"Rate:      {SAMPLE_RATE} Hz mono")
    print()

    # Step 1: MP3 → Ogg Opus via FFmpeg
    ogg_path = output_prefix + ".ogg"
    print("Encoding with FFmpeg (libopus)...")
    args = [
        "ffmpeg", "-y", "-v", "error",
        "-ss", str(start),
        "-t", duration_str,
        "-i", input_mp3,
        "-ar", str(SAMPLE_RATE),
        "-ac", "1",
        "-c:a", "libopus",
        "-b:a", "24k",
        "-frame_duration", str(FRAME_MS),
        "-packet_loss", "0",
        "-application", "audio",
        "-vbr", "on",
        "-compression_level", "10",
        ogg_path,
    ]
    subprocess.run(args, check=True)

    # Step 2: Parse Ogg → extract raw Opus packets
    packets = parse_ogg_extract_opus_packets(ogg_path)
    os.unlink(ogg_path)

    if not packets:
        print("Error: no Opus packets extracted")
        sys.exit(1)

    # Step 3: Write C array
    write_c_array(packets, output_prefix)

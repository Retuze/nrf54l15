"""Generate an embedded Opus C array from a melody.

Creates WAV, encodes via ffmpeg to Ogg Opus, then strips the Ogg
container and outputs length-prefixed raw Opus packets as a C header.
"""
import struct
import wave
import subprocess
import os

SAMPLE_RATE = 16000

# Twinkle Twinkle Little Star (C major)
C4, D4, E4, F4, G4, A4, B4 = 262, 294, 330, 349, 392, 440, 494

melody = [
    (C4, 0.4), (C4, 0.4), (G4, 0.4), (G4, 0.4), (A4, 0.4), (A4, 0.4), (G4, 0.8),
    (F4, 0.4), (F4, 0.4), (E4, 0.4), (E4, 0.4), (D4, 0.4), (D4, 0.4), (C4, 0.8),
    (G4, 0.4), (G4, 0.4), (F4, 0.4), (F4, 0.4), (E4, 0.4), (E4, 0.4), (D4, 0.8),
    (G4, 0.4), (G4, 0.4), (F4, 0.4), (F4, 0.4), (E4, 0.4), (E4, 0.4), (D4, 0.8),
    (C4, 0.4), (C4, 0.4), (G4, 0.4), (G4, 0.4), (A4, 0.4), (A4, 0.4), (G4, 0.8),
    (F4, 0.4), (F4, 0.4), (E4, 0.4), (E4, 0.4), (D4, 0.4), (D4, 0.4), (C4, 1.2),
]

def generate_wav(path):
    import math
    samples = []
    for freq, dur in melody:
        n = int(SAMPLE_RATE * dur)
        for i in range(n):
            if freq == 0:
                val = 0
            else:
                env = max(0.0, 1.0 - i / n) ** 0.3
                val = int(28000 * env * math.sin(2.0 * math.pi * freq * i / SAMPLE_RATE))
            val = max(-32768, min(32767, val))
            samples.append(val)
    pad_n = int(SAMPLE_RATE * 0.005)
    samples.extend([0] * pad_n)
    raw = struct.pack(f"<{len(samples)}h", *samples)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(raw)
    dur_s = len(samples) / SAMPLE_RATE
    print(f"WAV: {len(samples)} samples, {dur_s:.1f}s, {len(raw) / 1024:.1f} KB")

def encode_ogg_opus(wav_path, ogg_path):
    cmd = [
        "ffmpeg", "-y", "-v", "error",
        "-i", wav_path,
        "-c:a", "libopus",
        "-b:a", "24k",
        "-ar", str(SAMPLE_RATE),
        "-ac", "1",
        "-frame_duration", "20",
        "-application", "audio",
        "-vn",
        ogg_path,
    ]
    subprocess.run(cmd, check=True)
    size = os.path.getsize(ogg_path)
    print(f"Ogg Opus: {size} bytes ({size / 1024:.1f} KB)")

def extract_opus_packets(ogg_path):
    """Parse Ogg container and extract raw Opus packets.
    Returns list of bytes objects, one per Opus packet.
    Skips OpusHead and OpusTags header packets.
    """
    with open(ogg_path, "rb") as f:
        data = f.read()

    packets = []
    pos = 0
    while pos < len(data):
        if data[pos:pos + 4] != b"OggS":
            break

        header_type = data[pos + 5]
        n_segments = data[pos + 26]
        seg_table = data[pos + 27 : pos + 27 + n_segments]
        data_start = pos + 27 + n_segments

        # Calculate total data length for this page
        data_len = sum(seg_table)
        page_data = data[data_start : data_start + data_len]
        pos = data_start + data_len

        # Skip BOS page (OpusHead) and the following page (OpusTags)
        is_bos = (header_type & 0x02) != 0
        if is_bos:
            continue  # skip OpusHead page

        # After BOS, the next page is OpusTags — identified by being page 1
        # or by starting with "OpusTags"
        if len(packets) == 0 and page_data[:8] == b"OpusTags":
            continue  # skip OpusTags page

        # Extract packets from segments
        pkt_start = 0
        pkt_data = bytearray()
        for seg_len in seg_table:
            seg = page_data[pkt_start : pkt_start + seg_len]
            pkt_data.extend(seg)
            pkt_start += seg_len
            if seg_len < 255:
                # End of packet
                packets.append(bytes(pkt_data))
                pkt_data = bytearray()
        # Any remaining data after last segment (shouldn't happen)
        if len(pkt_data) > 0:
            packets.append(bytes(pkt_data))

    return packets

def write_c_header(packets, header_path):
    """Write Opus packets as a length-prefixed C array.
    Format: uint16_le num_packets, then for each packet: uint16_le len + data.
    """
    # Build the binary blob
    blob = bytearray()
    # Total number of packets as uint16 LE
    blob.extend(struct.pack("<H", len(packets)))
    for pkt in packets:
        # Length as uint16 LE
        blob.extend(struct.pack("<H", len(pkt)))
        blob.extend(pkt)

    lines = []
    lines.append("// Auto-generated embedded Opus audio (raw packets, no Ogg container).")
    lines.append(f"// {len(packets)} packets, {SAMPLE_RATE} Hz mono, 20ms frames")
    lines.append(f"// Total size: {len(blob)} bytes")
    lines.append("#ifndef EMBEDDED_AUDIO_H")
    lines.append("#define EMBEDDED_AUDIO_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append(f"#define EMBEDDED_AUDIO_SAMPLE_RATE {SAMPLE_RATE}")
    lines.append(f"#define EMBEDDED_AUDIO_PACKET_COUNT {len(packets)}")
    lines.append("")
    lines.append("// Format: uint16_le packet_count, then for each: uint16_le len + raw Opus data")
    lines.append("static const uint8_t embedded_opus_data[] = {")

    for i in range(0, len(blob), 16):
        chunk = blob[i : i + 16]
        hex_bytes = ", ".join(f"0x{b:02X}" for b in chunk)
        if i + 16 < len(blob):
            hex_bytes += ","
        lines.append(f"    {hex_bytes}")

    lines.append("};")
    lines.append(f"static const size_t embedded_opus_size = {len(blob)};")
    lines.append("")
    lines.append("#endif /* EMBEDDED_AUDIO_H */")
    lines.append("")

    with open(header_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Header: {header_path} ({len(lines)} lines, {len(blob)} bytes raw packets)")

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    opus_dir = os.path.dirname(script_dir)
    wav_path = os.path.join(script_dir, "_gen_melody.wav")
    ogg_path = os.path.join(script_dir, "_gen_melody.ogg")
    header_path = os.path.join(opus_dir, "embedded_audio.h")

    print("Generating melody WAV...")
    generate_wav(wav_path)
    print("Encoding to Ogg Opus...")
    encode_ogg_opus(wav_path, ogg_path)
    print("Extracting raw Opus packets...")
    packets = extract_opus_packets(ogg_path)
    print(f"  {len(packets)} packets extracted")
    total = sum(len(p) for p in packets)
    print(f"  {total} bytes of raw Opus data ({total / 1024:.1f} KB)")
    print("Writing embedded C header...")
    write_c_header(packets, header_path)

    os.unlink(wav_path)
    os.unlink(ogg_path)
    print(f"Done! Embedded audio written to {header_path}")

if __name__ == "__main__":
    main()

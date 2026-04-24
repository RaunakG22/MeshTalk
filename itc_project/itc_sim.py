from __future__ import annotations

import argparse
import collections
import csv
import dataclasses
import heapq
import os
import random
from typing import Dict, List, Optional, Tuple


def bytes_to_bits(data: bytes) -> List[int]:
    bits: List[int] = []
    for b in data:
        for i in range(7, -1, -1):
            bits.append((b >> i) & 1)
    return bits


@dataclasses.dataclass
class HuffmanNode:
    freq: int
    symbol: Optional[int] = None
    left: Optional["HuffmanNode"] = None
    right: Optional["HuffmanNode"] = None

    def __lt__(self, other: "HuffmanNode") -> bool:
        return self.freq < other.freq


def build_huffman_codes(data: bytes) -> Dict[int, List[int]]:
    freq = collections.Counter(data)
    heap: List[HuffmanNode] = [HuffmanNode(f, s) for s, f in freq.items()]
    heapq.heapify(heap)
    if len(heap) == 1:
        only = heapq.heappop(heap)
        return {only.symbol: [0]}  # type: ignore[index]
    while len(heap) > 1:
        a = heapq.heappop(heap)
        b = heapq.heappop(heap)
        heapq.heappush(heap, HuffmanNode(a.freq + b.freq, None, a, b))
    root = heap[0]
    codes: Dict[int, List[int]] = {}

    def dfs(node: HuffmanNode, prefix: List[int]) -> None:
        if node.symbol is not None:
            codes[node.symbol] = prefix[:] or [0]
            return
        if node.left is not None:
            dfs(node.left, prefix + [0])
        if node.right is not None:
            dfs(node.right, prefix + [1])

    dfs(root, [])
    return codes


def huffman_encode(data: bytes) -> Tuple[List[int], Dict[int, List[int]]]:
    codes = build_huffman_codes(data)
    out: List[int] = []
    for b in data:
        out.extend(codes[b])
    return out, codes


def huffman_decode(bits: List[int], codes: Dict[int, List[int]], out_len: int) -> bytes:
    rev = {tuple(v): k for k, v in codes.items()}
    cur: List[int] = []
    out = bytearray()
    for bit in bits:
        cur.append(bit)
        t = tuple(cur)
        if t in rev:
            out.append(rev[t])
            cur.clear()
            if len(out) == out_len:
                break
    return bytes(out)


def hamming74_encode(bits: List[int]) -> List[int]:
    out: List[int] = []
    for i in range(0, len(bits), 4):
        d = bits[i : i + 4]
        if len(d) < 4:
            d += [0] * (4 - len(d))
        d1, d2, d3, d4 = d
        p1 = d1 ^ d2 ^ d4
        p2 = d1 ^ d3 ^ d4
        p3 = d2 ^ d3 ^ d4
        out.extend([p1, p2, d1, p3, d2, d3, d4])
    return out


def hamming74_decode(bits: List[int], original_len: int) -> List[int]:
    out: List[int] = []
    for i in range(0, len(bits), 7):
        v = bits[i : i + 7]
        if len(v) < 7:
            break
        s1 = v[0] ^ v[2] ^ v[4] ^ v[6]
        s2 = v[1] ^ v[2] ^ v[5] ^ v[6]
        s3 = v[3] ^ v[4] ^ v[5] ^ v[6]
        err = s1 + (s2 << 1) + (s3 << 2)
        if 1 <= err <= 7:
            v[err - 1] ^= 1
        out.extend([v[2], v[4], v[5], v[6]])
        if len(out) >= original_len:
            break
    return out[:original_len]


def bsc_channel(bits: List[int], p: float, rng: random.Random) -> List[int]:
    return [(1 - b) if rng.random() < p else b for b in bits]


def bit_error_rate(a: List[int], b: List[int]) -> float:
    n = min(len(a), len(b))
    if n == 0:
        return 1.0
    err = sum(1 for i in range(n) if a[i] != b[i]) + abs(len(a) - len(b))
    return err / max(len(a), len(b))


def message_accuracy(src: bytes, rec: bytes) -> float:
    n = min(len(src), len(rec))
    if n == 0:
        return 0.0
    ok = sum(1 for i in range(n) if src[i] == rec[i])
    return ok / len(src)


def generate_text_source(size: int) -> bytes:
    base = (
        "Reliable communication over noisy channels using source coding and channel coding. "
        "This simulation evaluates BER, code rate, and reconstruction accuracy. "
    ).encode("utf-8")
    return (base * ((size // len(base)) + 1))[:size]


def run_once(noise: float, payload: bytes, rng: random.Random) -> Dict[str, float]:
    src_bits, codes = huffman_encode(payload)
    tx_bits = hamming74_encode(src_bits)
    rx_bits = bsc_channel(tx_bits, noise, rng)
    dec_src_bits = hamming74_decode(rx_bits, len(src_bits))
    reconstructed = huffman_decode(dec_src_bits, codes, len(payload))
    rec_bits = bytes_to_bits(reconstructed)
    return {
        "ber": bit_error_rate(bytes_to_bits(payload), rec_bits),
        "compression_ratio": (len(payload) * 8) / max(1, len(src_bits)),
        "code_rate": len(src_bits) / max(1, len(tx_bits)),
        "accuracy": message_accuracy(payload, reconstructed),
        "source_bits": float(len(src_bits)),
        "tx_bits": float(len(tx_bits)),
    }


def run_uncoded(noise: float, payload: bytes, rng: random.Random) -> Dict[str, float]:
    src_bits = bytes_to_bits(payload)
    rx_bits = bsc_channel(src_bits, noise, rng)
    rec = bytearray()
    for i in range(0, len(rx_bits), 8):
        chunk = rx_bits[i : i + 8]
        if len(chunk) < 8:
            break
        v = 0
        for b in chunk:
            v = (v << 1) | b
        rec.append(v)
    rec_bits = bytes_to_bits(bytes(rec))
    return {"ber": bit_error_rate(src_bits, rec_bits)}


def save_all_csv(
    out_dir: str,
    noises: List[float],
    ber_uncoded: List[float],
    ber_coded: List[float],
    metrics: Dict[str, float],
) -> None:
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "ber_vs_noise.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["noise", "ber_uncoded", "ber_coded"])
        for i, p in enumerate(noises):
            w.writerow([p, ber_uncoded[i], ber_coded[i]])

    with open(os.path.join(out_dir, "compression_comparison.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["scheme", "compression_ratio"])
        w.writerow(["None", 1.0])
        w.writerow(["Huffman", metrics["compression_ratio"]])

    with open(os.path.join(out_dir, "metrics_summary.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["metric", "value"])
        w.writerow(["source_coding", "huffman"])
        w.writerow(["channel_coding", "hamming74"])
        w.writerow(["channel_model", "bsc_channel"])
        w.writerow(["sample_noise", 0.08])
        w.writerow(["bit_error_rate", metrics["ber"]])
        w.writerow(["compression_ratio", metrics["compression_ratio"]])
        w.writerow(["code_rate", metrics["code_rate"]])
        w.writerow(["message_recovery_accuracy", metrics["accuracy"]])
        w.writerow(["source_bits", metrics["source_bits"]])
        w.writerow(["transmitted_bits", metrics["tx_bits"]])


def save_plots(
    out_dir: str, noises: List[float], ber_uncoded: List[float], ber_coded: List[float], compression_ratio: float
) -> bool:
    try:
        import matplotlib.pyplot as plt
    except ModuleNotFoundError:
        return False
    os.makedirs(out_dir, exist_ok=True)

    plt.figure(figsize=(7, 4))
    plt.plot(noises, ber_uncoded, marker="o", label="Uncoded")
    plt.plot(noises, ber_coded, marker="s", label="Hamming(7,4)+Huffman")
    plt.xlabel("Noise probability")
    plt.ylabel("BER")
    plt.title("BER vs Noise")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "ber_vs_noise.png"), dpi=160)
    plt.close()

    plt.figure(figsize=(7, 4))
    plt.plot(noises, ber_uncoded, marker="o", label="Uncoded")
    plt.plot(noises, ber_coded, marker="s", label="Coded")
    plt.xlabel("Noise probability")
    plt.ylabel("BER")
    plt.title("Coded vs Uncoded")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "coded_vs_uncoded.png"), dpi=160)
    plt.close()

    plt.figure(figsize=(7, 4))
    plt.bar(["None", "Huffman"], [1.0, compression_ratio])
    plt.ylabel("Compression ratio")
    plt.title("Compression comparison")
    plt.grid(True, axis="y", alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "compression_comparison.png"), dpi=160)
    plt.close()
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description="Fixed pipeline: Huffman + Hamming(7,4) + BSC")
    parser.add_argument("--message-size", type=int, default=1024)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--out-dir", default="results")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    payload = generate_text_source(args.message_size)
    noises = [0.01, 0.03, 0.05, 0.08, 0.10, 0.15]
    ber_uncoded: List[float] = []
    ber_coded: List[float] = []

    for p in noises:
        ber_uncoded.append(run_uncoded(p, payload, rng)["ber"])
        ber_coded.append(run_once(p, payload, rng)["ber"])

    sample_noise = 0.08
    main_metrics = run_once(sample_noise, payload, rng)
    save_all_csv(args.out_dir, noises, ber_uncoded, ber_coded, main_metrics)
    plots_ok = save_plots(args.out_dir, noises, ber_uncoded, ber_coded, main_metrics["compression_ratio"])

    print("=== Reliable Communication System Results ===")
    print("Source coding: huffman")
    print("Channel coding: hamming74")
    print("Channel model: bsc_channel")
    print(f"Noise (sample): {sample_noise:.2f}")
    print(f"Bit Error Rate (BER): {main_metrics['ber']:.6f}")
    print(f"Compression Ratio: {main_metrics['compression_ratio']:.4f}")
    print(f"Code Rate: {main_metrics['code_rate']:.4f}")
    print(f"Message Recovery Accuracy: {main_metrics['accuracy']:.4f}")
    if plots_ok:
        print(f"Output plots: {os.path.abspath(args.out_dir)}")
    else:
        print(f"matplotlib not installed; CSV output saved to: {os.path.abspath(args.out_dir)}")


if __name__ == "__main__":
    main()

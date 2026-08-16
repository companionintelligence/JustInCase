#!/usr/bin/env python3
"""
Zero-dependency BitTorrent v1 .torrent file & Magnet link generator.
Companion Intelligence / JustInCase
"""

import sys
import os
import hashlib
import time
import argparse
import urllib.parse

DEFAULT_TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://tracker.openbittorrent.com:6969/announce",
    "http://tracker.openbittorrent.com:80/announce",
    "udp://exodus.desync.com:6969/announce",
]

def bencode(obj):
    if isinstance(obj, int):
        return f"i{obj}e".encode("ascii")
    elif isinstance(obj, (bytes, bytearray)):
        return f"{len(obj)}:".encode("ascii") + obj
    elif isinstance(obj, str):
        b = obj.encode("utf-8")
        return f"{len(b)}:".encode("ascii") + b
    elif isinstance(obj, list):
        return b"l" + b"".join(bencode(x) for x in obj) + b"e"
    elif isinstance(obj, dict):
        # Keys must be sorted in raw byte order
        encoded_items = []
        for k in sorted(obj.keys(), key=lambda x: x.encode("utf-8") if isinstance(x, str) else x):
            v = obj[k]
            encoded_items.append(bencode(k))
            encoded_items.append(bencode(v))
        return b"d" + b"".join(encoded_items) + b"e"
    else:
        raise TypeError(f"Cannot bencode type: {type(obj)}")

def choose_piece_length(total_bytes):
    # Select appropriate piece length based on total size
    # aiming for 1000 - 2500 pieces
    if total_bytes < 50 * 1024 * 1024:        # < 50 MB
        return 256 * 1024                     # 256 KB
    elif total_bytes < 500 * 1024 * 1024:      # < 500 MB
        return 512 * 1024                     # 512 KB
    elif total_bytes < 5 * 1024 * 1024 * 1024: # < 5 GB
        return 1024 * 1024                    # 1 MB
    elif total_bytes < 20 * 1024 * 1024 * 1024:# < 20 GB
        return 2 * 1024 * 1024                # 2 MB
    elif total_bytes < 80 * 1024 * 1024 * 1024:# < 80 GB
        return 4 * 1024 * 1024                # 4 MB
    else:
        return 8 * 1024 * 1024                # 8 MB

def create_torrent(target_path, output_torrent=None, comment="JustInCase Knowledge Archive", trackers=None, piece_size_bytes=None, webseeds=None):
    target_path = os.path.abspath(target_path)
    if not os.path.exists(target_path):
        raise FileNotFoundError(f"Target path does not exist: {target_path}")

    trackers = trackers or DEFAULT_TRACKERS
    webseeds = webseeds or []

    is_dir = os.path.isdir(target_path)
    base_name = os.path.basename(target_path.rstrip("/\\"))

    file_list = []
    total_size = 0

    if is_dir:
        for root, dirs, files in os.walk(target_path):
            dirs.sort()
            for f in sorted(files):
                if f.startswith(".") or f == output_torrent:
                    continue
                full_path = os.path.join(root, f)
                rel_path = os.path.relpath(full_path, target_path)
                size = os.path.getsize(full_path)
                path_parts = rel_path.split(os.sep)
                file_list.append({"full_path": full_path, "rel_path": rel_path, "parts": path_parts, "length": size})
                total_size += size
    else:
        size = os.path.getsize(target_path)
        file_list.append({"full_path": target_path, "rel_path": base_name, "parts": [base_name], "length": size})
        total_size = size

    if total_size == 0:
        raise ValueError("Cannot create torrent for empty directory/file (0 bytes)")

    piece_len = piece_size_bytes or choose_piece_length(total_size)
    num_pieces = (total_size + piece_len - 1) // piece_len

    print(f"Target: {base_name} ({'directory with ' + str(len(file_list)) + ' files' if is_dir else 'single file'})")
    print(f"Total size: {total_size / (1024**3):.2f} GiB ({total_size:,} bytes)")
    print(f"Piece size: {piece_len // 1024} KiB ({num_pieces:,} pieces)")

    # Read streams and compute piece hashes
    pieces = bytearray()
    current_piece = bytearray()
    processed_bytes = 0
    start_time = time.time()
    last_print = 0

    for item in file_list:
        with open(item["full_path"], "rb") as f:
            while True:
                needed = piece_len - len(current_piece)
                chunk = f.read(needed)
                if not chunk:
                    break
                current_piece.extend(chunk)
                processed_bytes += len(chunk)

                if len(current_piece) == piece_len:
                    pieces.extend(hashlib.sha1(current_piece).digest())
                    current_piece.clear()

                now = time.time()
                if now - last_print > 1.0:
                    pct = (processed_bytes / total_size) * 100
                    speed = (processed_bytes / (1024 * 1024)) / max(1, (now - start_time))
                    print(f"\rHashing: {pct:.1f}% ({processed_bytes / (1024**3):.2f}/{total_size / (1024**3):.2f} GiB) @ {speed:.1f} MB/s", end="", flush=True)
                    last_print = now

    if len(current_piece) > 0:
        pieces.extend(hashlib.sha1(current_piece).digest())
        current_piece.clear()

    elapsed = max(1, time.time() - start_time)
    print(f"\rHashing: 100.0% ({total_size / (1024**3):.2f} GiB) in {elapsed:.1f}s ({total_size / (1024*1024*elapsed):.1f} MB/s)  ")

    info_dict = {
        "piece length": piece_len,
        "pieces": bytes(pieces),
        "name": base_name,
    }

    if is_dir:
        info_dict["files"] = [{"length": f["length"], "path": f["parts"]} for f in file_list]
    else:
        info_dict["length"] = total_size

    info_bencoded = bencode(info_dict)
    info_hash = hashlib.sha1(info_bencoded).hexdigest()

    metainfo = {
        "announce": trackers[0],
        "announce-list": [[t] for t in trackers],
        "comment": comment,
        "created by": "JustInCase / Companion Intelligence",
        "creation date": int(time.time()),
        "info": info_dict,
    }

    if webseeds:
        metainfo["url-list"] = webseeds

    if not output_torrent:
        output_torrent = f"{base_name}.torrent"

    with open(output_torrent, "wb") as f:
        f.write(bencode(metainfo))

    # Construct Magnet URI
    tr_params = "&".join(f"tr={urllib.parse.quote(t)}" for t in trackers)
    magnet_uri = f"magnet:?xt=urn:btih:{info_hash}&dn={urllib.parse.quote(base_name)}&{tr_params}"

    print(f"\n✅ Created: {output_torrent}")
    print(f"Info Hash:  {info_hash}")
    print(f"Magnet URI:\n{magnet_uri}\n")

    return output_torrent, info_hash, magnet_uri

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create BitTorrent v1 .torrent and magnet link")
    parser.add_argument("target", help="File or directory to package into torrent")
    parser.add_argument("-o", "--output", help="Output .torrent file path")
    parser.add_argument("-c", "--comment", default="JustInCase Offline Knowledge Pack", help="Torrent comment")
    parser.add_argument("-p", "--piece-size-kb", type=int, help="Piece size in KiB (e.g. 512, 1024, 2048, 4096)")
    parser.add_argument("-t", "--tracker", action="append", help="Custom tracker URL (can be specified multiple times)")
    parser.add_argument("-w", "--webseed", action="append", help="HTTP WebSeed URL (can be specified multiple times)")

    args = parser.parse_args()

    piece_bytes = (args.piece_size_kb * 1024) if args.piece_size_kb else None
    create_torrent(
        args.target,
        output_torrent=args.output,
        comment=args.comment,
        trackers=args.tracker,
        piece_size_bytes=piece_bytes,
        webseeds=args.webseed
    )

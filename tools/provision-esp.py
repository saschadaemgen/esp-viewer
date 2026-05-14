#!/usr/bin/env python3
"""
provision-esp.py - NVS-Provisioning fuer ESP-Innenmonitor

ESP-Saison 2 Tag 2

Schreibt einen Bearer-Token in die NVS-Partition des ESP32-P4
via esptool. Der ESP liest den Token beim naechsten Boot aus
NVS-Namespace "unifix", Key "device_token".

Voraussetzungen:
  - ESP-IDF aktiviert (export.ps1 / export.sh)
  - esptool im PATH
  - nvs_partition_gen.py erreichbar (kommt mit ESP-IDF)
  - ESP-Geraet via USB verbunden (COM4 default auf Windows)

Verwendung:
  python tools/provision-esp.py <token> [--port COM4] [--erase]

Beispiel:
  python tools/provision-esp.py DwCR88Joi5KZ3yAUFKidRh__1WhN-fTINpSnrrn-St0
  python tools/provision-esp.py <token> --port COM4 --erase

Saison-2-Praxis:
  Sascha generiert Token im unifix-Admin-CLI, kopiert hier rein.
  Spaeter (Saison 14+): Master-Chat-CLI exportiert provisioning.json,
  dieses Tool nimmt JSON-Input statt Plain-Token.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Pfad-Konfiguration
PROJECT_ROOT = Path(__file__).resolve().parent.parent
PARTITIONS_CSV = PROJECT_ROOT / "partitions.csv"

# Default-NVS-Partition-Werte (werden aus partitions.csv ueberschrieben)
DEFAULT_NVS_OFFSET = 0x9000
DEFAULT_NVS_SIZE = 0x6000

# NVS-Eintrag-Definition
NVS_NAMESPACE = "unifix"
NVS_KEY_TOKEN = "device_token"


def find_nvs_partition():
    """
    Liest partitions.csv und sucht die nvs-Partition.

    Format: name,type,subtype,offset,size,flags
    Beispiel-Zeile: nvs,data,nvs,0x9000,0x6000,

    Gibt (offset, size) zurueck oder Default wenn nicht gefunden.
    """
    if not PARTITIONS_CSV.exists():
        print(f"WARNING: {PARTITIONS_CSV} not found, using defaults")
        return DEFAULT_NVS_OFFSET, DEFAULT_NVS_SIZE

    with open(PARTITIONS_CSV, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 5 and parts[0] == "nvs" and parts[2] == "nvs":
                offset = int(parts[3], 0)
                size = int(parts[4], 0)
                return offset, size

    print(f"WARNING: nvs partition not found in {PARTITIONS_CSV}, using defaults")
    return DEFAULT_NVS_OFFSET, DEFAULT_NVS_SIZE


def find_nvs_partition_gen():
    """
    Sucht nvs_partition_gen.py in der aktiven ESP-IDF-Installation.

    Gibt vollen Pfad zurueck oder None wenn nicht gefunden.
    """
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        print("ERROR: IDF_PATH not set. Run export.ps1 / export.sh first.")
        return None

    candidate = Path(idf_path) / "components" / "nvs_flash" / \
                "nvs_partition_generator" / "nvs_partition_gen.py"

    if not candidate.exists():
        print(f"ERROR: nvs_partition_gen.py not found at {candidate}")
        return None

    return candidate


def validate_token(token):
    """
    Prueft den Token-String auf Base64URL-Format.
    Erlaubt: A-Z, a-z, 0-9, -, _
    Laenge: 1 bis 127 Zeichen
    """
    if not token:
        return False, "Token is empty"
    if len(token) > 127:
        return False, f"Token too long ({len(token)} chars, max 127)"

    valid_chars = set(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_"
    )
    invalid = [c for c in token if c not in valid_chars]
    if invalid:
        return False, f"Invalid characters in token: {set(invalid)}"

    return True, None


def create_nvs_csv(token, csv_path):
    """
    Erzeugt eine NVS-CSV-Datei mit dem Token-Eintrag.
    """
    with open(csv_path, "w", newline="\n") as f:
        f.write("key,type,encoding,value\n")
        f.write(f"{NVS_NAMESPACE},namespace,,\n")
        f.write(f"{NVS_KEY_TOKEN},data,string,{token}\n")


def generate_nvs_bin(csv_path, bin_path, nvs_size, nvs_gen_path):
    """
    Ruft nvs_partition_gen.py auf um die NVS-Partition zu erzeugen.
    """
    cmd = [
        sys.executable,
        str(nvs_gen_path),
        "generate",
        str(csv_path),
        str(bin_path),
        str(nvs_size),
    ]
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("ERROR: nvs_partition_gen.py failed")
        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        return False
    return True


def flash_nvs_bin(bin_path, offset, port, erase_first):
    """
    Flasht die NVS-Partition via esptool.

    erase_first: wenn True, vorher erase_region damit alter Stand weg ist.
    """
    if erase_first:
        cmd_erase = [
            sys.executable, "-m", "esptool",
            "--chip", "esp32p4",
            "--port", port,
            "erase_region",
            hex(offset),
            "0x6000",  # NVS-Size aus partitions.csv
        ]
        print(f"Running: {' '.join(cmd_erase)}")
        result = subprocess.run(cmd_erase)
        if result.returncode != 0:
            print("ERROR: erase_region failed")
            return False

    cmd_flash = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32p4",
        "--port", port,
        "write_flash",
        hex(offset),
        str(bin_path),
    ]
    print(f"Running: {' '.join(cmd_flash)}")
    result = subprocess.run(cmd_flash)
    if result.returncode != 0:
        print("ERROR: esptool write_flash failed")
        return False

    return True


def main():
    parser = argparse.ArgumentParser(
        description="ESP-Saison-2 Token-Provisioning via NVS-Partition-Flash",
    )
    parser.add_argument("token", help="Bearer-Token (Base64URL, max 127 chars)")
    parser.add_argument("--port", default="COM4",
                        help="Serial port (default: COM4)")
    parser.add_argument("--erase", action="store_true",
                        help="Erase NVS region before writing (recommended)")
    parser.add_argument("--keep-tempfiles", action="store_true",
                        help="Don't delete tempdir after flash (debug)")

    args = parser.parse_args()

    # Token validieren
    ok, msg = validate_token(args.token)
    if not ok:
        print(f"ERROR: {msg}")
        return 1

    print(f"Token length: {len(args.token)} chars (validated)")

    # nvs_partition_gen.py suchen
    nvs_gen = find_nvs_partition_gen()
    if not nvs_gen:
        return 1
    print(f"Using NVS-Generator: {nvs_gen}")

    # NVS-Partition aus partitions.csv lesen
    offset, size = find_nvs_partition()
    print(f"NVS-Partition: offset={hex(offset)}, size={hex(size)}")

    # Temp-Verzeichnis
    tmpdir = tempfile.mkdtemp(prefix="provision-esp-")
    csv_path = Path(tmpdir) / "provisioning.csv"
    bin_path = Path(tmpdir) / "provisioning.bin"

    try:
        # CSV erzeugen
        create_nvs_csv(args.token, csv_path)
        print(f"Created CSV: {csv_path}")

        # NVS-Bin generieren
        if not generate_nvs_bin(csv_path, bin_path, size, nvs_gen):
            return 1
        print(f"Generated NVS-Bin: {bin_path}")

        # Flashen
        if not flash_nvs_bin(bin_path, offset, args.port, args.erase):
            return 1

        print("=" * 60)
        print("  SUCCESS - Token written to NVS")
        print("  Reset the ESP (or unplug/replug) to boot with token")
        print("=" * 60)
        return 0

    finally:
        if args.keep_tempfiles:
            print(f"Tempfiles kept at: {tmpdir}")
        else:
            shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

import argparse
import hashlib
import json
import zipfile
from pathlib import Path

from analyse import DEFAULT_OUTPUT, ROOT, read_json, save_json


def package(output, destination):
    source = Path(__file__).resolve().parent
    allowed_source = {".py", ".cs", ".ps1", ".json", ".md", ".txt"}
    allowed_result = {".csv", ".json", ".step", ".npz", ".msh", ".png", ".obj", ".glb", ".xml"}
    files = sorted(path for path in source.iterdir() if path.is_file() and path.suffix.lower() in allowed_source)
    files.extend(sorted(path for path in output.rglob("*") if path.is_file() and path.suffix.lower() in allowed_result
                        and "__pycache__" not in path.parts and path.name != "package_manifest.json" and "training" not in path.parts))
    pdf = ROOT / "output" / "pdf" / "TALOS_CAE_Assessment.pdf"
    files.append(pdf)
    layout = read_json(output / "report_layout.json")
    if layout["page_count"] != 15:
        raise ValueError("Unexpected report page count; inspect layout before packaging")
    checks = read_json(output / "artifact_checks.json")
    if not checks["raster_visual_review_complete"] or layout["raster_visual_qa"] == "pending":
        raise ValueError("Complete visual report review and artifact checks before packaging")
    if checks["pdf_sha256"] != hashlib.sha256(pdf.read_bytes()).hexdigest():
        raise ValueError("PDF changed since artifact checks; review and verify it again")
    manifest = {"status": "Preliminary offline CAE; no trained policy or hardware validation",
                "excluded": ["firmware", "flash backups", "service logs", "credentials", "training outputs", "Python caches"],
                "files": [{"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size,
                           "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in files]}
    save_json(output / "package_manifest.json", manifest)
    files.append(output / "package_manifest.json")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in files:
            archive.write(path, path.relative_to(ROOT).as_posix())
        archive.writestr("START_HERE.txt", "TALOS preliminary CAE and offline simulation\n\nRead tools/cae/README.md and output/pdf/TALOS_CAE_Assessment.pdf.\nNo hardware interface or trained policy is included.\n")
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None:
            raise ValueError("Archive CRC test failed")
        for entry in manifest["files"]:
            if hashlib.sha256(archive.read(entry["path"])).hexdigest() != entry["sha256"]:
                raise ValueError(f"Archive hash mismatch: {entry['path']}")
    print(json.dumps({"zip": str(destination), "bytes": destination.stat().st_size, "files": len(files) + 1,
                      "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(), "crc_and_readback_sha256": "passed"}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--zip", type=Path, default=DEFAULT_OUTPUT / "TALOS_CAE_Simulation_Package.zip")
    arguments = parser.parse_args()
    package(arguments.output, arguments.zip)

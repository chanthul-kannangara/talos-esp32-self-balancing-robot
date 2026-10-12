import argparse
import hashlib
import json
import py_compile
from pathlib import Path

from pypdf import PdfReader

from analyse import DEFAULT_OUTPUT, ROOT, read_json, save_json


def verify(output, visual_review_complete):
    compiled = []
    for path in sorted(Path(__file__).resolve().parent.glob("*.py")):
        py_compile.compile(str(path), doraise=True)
        compiled.append(path.name)
    before = read_json(output / "cad-source-sha256-before.json")
    after = read_json(output / "cad-source-sha256-after.json")
    assert before == after
    results = read_json(output / "results.json")
    fea = read_json(output / "fea_results.json")
    simulation = read_json(output / "simulation" / "validation.json")
    assert results["validation"]["fourbar_closure_max_m"] < 1e-9
    assert fea["meshes"][-1]["force_balance_residual_n"] < 1e-7
    assert fea["meshes"][-1]["moment_balance_residual_nm"] < 1e-8
    assert simulation["environment_checker"] == "passed"
    assert simulation["trained_policy"] is False and simulation["hardware_validated"] is False
    for filename, key in [("talos.xml", "model_xml_sha256"), ("model_assumptions.json", "model_assumptions_sha256")]:
        assert simulation[key] == hashlib.sha256((output / "simulation" / filename).read_bytes()).hexdigest()
    pdf = ROOT / "output" / "pdf" / "TALOS_CAE_Assessment.pdf"
    reader = PdfReader(pdf)
    assert len(reader.pages) == 15
    links = []
    for number, page in enumerate(reader.pages, start=1):
        text = page.extract_text()
        assert f"{number:02d} /" in text
        assert "TALOS / ENGINEERING ASSESSMENT" in text
        for reference in page.get("/Annots", []):
            annotation = reference.get_object()
            if annotation.get("/A", {}).get("/S") == "/URI":
                links.append(str(annotation["/A"]["/URI"]))
    assert len(links) == 21 and all(link.startswith("https://") for link in links)
    layout = read_json(output / "report_layout.json")
    assert [section["page"] for section in layout["section_start_pages"]] == list(range(1, 16))
    if visual_review_complete:
        layout["raster_visual_qa"] = "All 15 pages visually inspected; chart label repaired and final affected page re-inspected"
        save_json(output / "report_layout.json", layout)
    summary = {"python_compilation": compiled, "saved_cad_source_hashes_unchanged": len(before),
               "calculation_closure_check": "passed", "fea_force_moment_checks": "passed",
               "simulation_api_checks": "passed", "untuned_simulation_baseline": "all five runs fell; not a balance success",
               "pdf_pages": len(reader.pages), "pdf_section_pagination": "passed", "reference_links": len(links),
               "pdf_sha256": hashlib.sha256(pdf.read_bytes()).hexdigest(), "raster_visual_review_complete": visual_review_complete,
               "no_hardware_test_or_trained_policy": True}
    save_json(output / "artifact_checks.json", summary)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--visual-review-complete", action="store_true")
    arguments = parser.parse_args()
    verify(arguments.output, arguments.visual_review_complete)

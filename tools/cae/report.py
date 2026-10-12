import argparse
import csv
import hashlib
import json
import shutil
from collections import defaultdict
from html import escape
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from PIL import Image as PillowImage
from pypdf import PdfReader
from reportlab.graphics.shapes import Drawing, Line, Rect, String
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import BaseDocTemplate, Frame, Image, PageBreak, PageTemplate, Paragraph, Spacer, Table, TableStyle

from analyse import COLORS, DEFAULT_OUTPUT, ROOT, finish_figure, read_json, save_json

INK = colors.HexColor("#203a36")
TEAL = colors.HexColor("#12665b")
PALE = colors.HexColor("#edf5f2")
AMBER = colors.HexColor("#a65b19")
GREY = colors.HexColor("#576963")
WIDTH = A4[0] - 84


class AssessmentDocument(BaseDocTemplate):
    def __init__(self, path, **kwargs):
        super().__init__(str(path), pagesize=A4, leftMargin=42, rightMargin=42, topMargin=57, bottomMargin=42, **kwargs)
        self.sections = []
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
        self.addPageTemplates(PageTemplate(id="assessment", frames=frame, onPage=self.decorate))

    def decorate(self, canvas, document):
        canvas.saveState()
        canvas.setFillColor(TEAL)
        canvas.rect(0, A4[1] - 7, A4[0], 7, stroke=0, fill=1)
        canvas.setFont("TalosBold", 9)
        canvas.drawString(42, A4[1] - 32, "TALOS / ENGINEERING ASSESSMENT")
        canvas.setFillColor(GREY)
        canvas.setFont("Talos", 8)
        canvas.drawRightString(A4[0] - 42, A4[1] - 32, "11 OCT 2026 | PRELIMINARY")
        canvas.setStrokeColor(colors.HexColor("#dce5df"))
        canvas.line(42, 32, A4[0] - 42, 32)
        canvas.setFont("Talos", 8)
        canvas.drawString(42, 20, "CAD-informed scenarios. Not a certified structure or hardware-ready policy.")
        canvas.drawRightString(A4[0] - 42, 20, str(document.page))
        canvas.restoreState()

    def afterFlowable(self, flowable):
        if isinstance(flowable, Paragraph) and flowable.style.name == "Section":
            self.sections.append({"title": flowable.getPlainText(), "page": self.page})


def font_setup():
    regular = Path("C:/Windows/Fonts/segoeui.ttf")
    bold = Path("C:/Windows/Fonts/segoeuib.ttf")
    if regular.exists() and bold.exists():
        pdfmetrics.registerFont(TTFont("Talos", str(regular)))
        pdfmetrics.registerFont(TTFont("TalosBold", str(bold)))
    else:
        pdfmetrics.registerFont(pdfmetrics.Font("Talos", "Helvetica", "WinAnsiEncoding"))
        pdfmetrics.registerFont(pdfmetrics.Font("TalosBold", "Helvetica-Bold", "WinAnsiEncoding"))
    pdfmetrics.registerFontFamily("Talos", normal="Talos", bold="TalosBold", italic="Talos", boldItalic="TalosBold")


def image_fit(path, width=WIDTH, height=300):
    with PillowImage.open(path) as source:
        scale = min(width / source.width, height / source.height)
        return Image(str(path), width=source.width * scale, height=source.height * scale)


def report(output, destination):
    font_setup()
    results = read_json(output / "results.json")
    fea = read_json(output / "fea_results.json")
    joints = read_json(output / "bolt_joint_results.json")
    validation = read_json(output / "simulation" / "validation.json")
    model_config = read_json(output / "simulation" / "model_assumptions.json")
    inputs = read_json(output / "inputs.json")
    if (results["example_mass_kg_not_measured"] != 2 or inputs["servo_voltage_v"] != 6
            or inputs["wheel_diameter_m"] != 0.085 or model_config["total_mass_kg_example_not_measured"] != 2
            or abs(model_config["initial_left_driven_hip_height_m"] - 0.16259235289359916) > 1e-8):
        raise SystemExit("This dated narrative requires the default 2 kg, 6 V, 85 mm, q50 assessment. Regenerate default results/model, or update the narrative for changed assumptions.")
    for filename, key in [("talos.xml", "model_xml_sha256"), ("model_assumptions.json", "model_assumptions_sha256")]:
        if validation.get(key) != hashlib.sha256((output / "simulation" / filename).read_bytes()).hexdigest():
            raise SystemExit("Model changed since validation. Run talos_env.py before generating the assessment.")
    sources = read_json(Path(__file__).with_name("sources.json"))
    figures = output / "figures"
    evidence = output / "evidence"
    evidence.mkdir(exist_ok=True)
    for filename, original in [
        ("robot-user-photo.png", "codex-clipboard-a6695240-a524-4887-aca7-d5126cf7980f.png"),
        ("hip-encoder-user-photo.png", "codex-clipboard-ef991e9c-1f6b-49eb-a337-ca3f2a0f46d4.png"),
    ]:
        photo = Path("C:/Users/cooln/AppData/Local/Temp") / original
        if not (evidence / filename).exists() and photo.exists():
            shutil.copy2(photo, evidence / filename)
    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle(name="Section", fontName="TalosBold", fontSize=23, leading=28, textColor=INK, spaceAfter=15))
    styles.add(ParagraphStyle(name="BodyTalos", fontName="Talos", fontSize=11, leading=15, textColor=INK, spaceAfter=9))
    styles.add(ParagraphStyle(name="SubTalos", fontName="TalosBold", fontSize=13, leading=17, textColor=TEAL, spaceBefore=7, spaceAfter=7))
    styles.add(ParagraphStyle(name="CaptionTalos", fontName="Talos", fontSize=9, leading=12, textColor=GREY, spaceAfter=8))
    styles.add(ParagraphStyle(name="TableTalos", fontName="Talos", fontSize=9.3, leading=12.2, textColor=INK))
    styles.add(ParagraphStyle(name="HeadTalos", fontName="TalosBold", fontSize=9.3, leading=12.2, textColor=colors.white))
    styles.add(ParagraphStyle(name="CodeTalos", fontName="Courier", fontSize=9.3, leading=13, textColor=INK, spaceAfter=9))
    styles.add(ParagraphStyle(name="RefTalos", fontName="Talos", fontSize=9.3, leading=12.3, textColor=INK, spaceAfter=6))
    story = []

    def paragraph(text, style="BodyTalos"):
        return Paragraph(text, styles[style])

    def section(number, title):
        if story:
            story.append(PageBreak())
        story.append(paragraph(f"{number:02d} / {title}", "Section"))

    def body(text):
        story.append(paragraph(text))

    def sub(text):
        story.append(paragraph(text, "SubTalos"))

    def caption(text):
        story.append(paragraph(text, "CaptionTalos"))

    def formula(text):
        story.append(paragraph(escape(text).replace("\n", "<br/>"), "CodeTalos"))

    def table(headers, records, widths):
        data = [[paragraph(escape(str(value)), "HeadTalos") for value in headers]]
        data.extend([[paragraph(escape(str(value)), "TableTalos") for value in record] for record in records])
        result = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT")
        result.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), TEAL), ("ROWBACKGROUNDS", (0, 1), (-1, -1), [PALE, colors.white]),
            ("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 8),
            ("RIGHTPADDING", (0, 0), (-1, -1), 8), ("TOPPADDING", (0, 0), (-1, -1), 6),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 6), ("LINEBELOW", (0, -1), (-1, -1), 0.5, colors.HexColor("#d6e4dd")),
        ]))
        story.extend([result, Spacer(1, 9)])

    def callout(title, text):
        block = Table([[paragraph(f"<b>{title}</b><br/>{text}")]], colWidths=[WIDTH])
        block.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), PALE), ("BOX", (0, 0), (-1, -1), 0.7, colors.HexColor("#bfd5ca")),
                                   ("LEFTPADDING", (0, 0), (-1, -1), 12), ("RIGHTPADDING", (0, 0), (-1, -1), 12),
                                   ("TOPPADDING", (0, 0), (-1, -1), 9), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]))
        story.extend([block, Spacer(1, 10)])

    def figure(name, description, height=260):
        story.append(image_fit(figures / name, height=height))
        caption(description)

    section(1, "What the engineering says")
    body("<b>Goal:</b> retain the working leg-position hold, add controlled height changes, and establish dependable wheel balance before attempting obstacles or automatic recovery.")
    figure("simulation_cad.png", "Actual CAD visual meshes in the new offline four-bar model. Wheel geometry and collision shapes are simplified; this is not a demonstrated balancing pose.", 270)
    table(["Priority", "Finding", "Action"], [
        ["1 / mechanics", "Mostly M4 bolts rotate directly against PLA.", "Measure clearance, binding and play; dimension proper pivot hardware."],
        ["2 / sensing", "Two AS5600s measure hip angles, not wheel speed.", "Validate magnetic fields and per-side height calibration."],
        ["3 / power", "Leg converter rating has limited stall-current headroom.", "Measure both servo rails and battery during a fault."],
        ["4 / control", "Fixed-height hold is reported; dynamic balance still fails.", "Preserve the hold baseline and commission balance in stages."],
    ], [88, 209, WIDTH - 297])
    callout("Delivered, with limits", "CAD extraction, calculations, one solved upper-link solid FEA and an executable simulation/training starter. No trained policy, full-robot structural certification, hardware motion, upload or website change is claimed.")

    section(2, "As built: evidence and unknowns")
    table(["Input", "Evidence used"], [
        ["Actuators", "2 SM-S4315R wheel servos; 2 continuous 25 kgf cm leg servos [S1-S3]."],
        ["Supply", "User: servo converters set to 6 V; 3 Ah battery; separate logic and leg bucks; wheel buck-boost. Logic output not confirmed."],
        ["Printed structure", "User: PLA, 50% infill, 0.16 mm layers. Walls, print orientation and mechanical properties unknown."],
        ["Joints", "User: M4 for most joints; motion directly against PLA. Other sizes, grade, preload and thread locations unknown."],
        ["Feedback", "MPU6050; one AS5600 at each driven hip in side housings. Camera currently disconnected."],
        ["Mass and height", "Assembled mass not supplied. Reported 16.2-29.2 cm, described as bottom to leg pivot; exact reference needs reconciliation with CAD."],
    ], [111, WIDTH - 111])
    photos = [evidence / "robot-user-photo.png", evidence / "hip-encoder-user-photo.png"]
    if all(path.exists() for path in photos):
        row = Table([[image_fit(path, width=235, height=178) for path in photos]], colWidths=[WIDTH / 2] * 2)
        row.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "MIDDLE")]))
        story.append(row)
        caption("User-supplied photographs: assembled crouched robot and encoder housing. Not live observations, dimensional metrology or measured rail voltage.")
    body("<b>Latest behavior clarification:</b> earlier firmware holds posture after <b>stand</b>; a separate balance command still cannot balance the robot. The hold is a useful baseline, not proof of dynamic stability. The exact earlier source revision is not yet identified.")
    caption("All 2 kg calculations below are illustrative. No figure silently treats the structural CAD mass as the complete robot mass.")

    section(3, "CAD geometry and real height")
    figure("cad_linkage.png", "Reconstructed left planar four-bar. q is the driven upper-link angle from vertical down, not the raw AS5600 absolute angle.", 230)
    table(["Native dimension", "Value", "Meaning"], [
        ["A-B", "100.00 mm", "Driven hip to driven knee"], ["B-C", "24.81 mm", "Lower-link coupler spacing"],
        ["C-D", "103.51 mm", "Passive support link"], ["C-W / B-W", "124.08 / 99.27 mm", "Follower / knee to axle"],
        ["Left D-A offset", "+37.43, +34.24 mm", "Fore-aft and vertical; right differs"],
        ["Wheel radius", "42.50 mm", "From specified 85 mm tire [S9]"],
    ], [136, 144, WIDTH - 280])
    body("The 13-component native assembly contains only five mates and incomplete linkage closure. Native driven-knee mismatch is <b>6.81 mm left / 5.10 mm right</b>; the left follower mismatch is 2.31 mm. The model repairs planar closure mathematically and does not alter your original assembly.")
    body("The driven-hip reach has an absolute triangle upper bound of <b>241.77 mm</b> with the interpreted bores and 85 mm tires. The near-extended sampled left pose is about 235.30 mm to the hip, plus 57.75 mm to the head top = <b>293.06 mm to the body top</b>. Thus 29.2 cm may use another reference or a different physical geometry; it is not a validated ground-to-hip value for this CAD.")
    caption("Measure ground-to-A and ground-to-head-top at the same straight and bent poses. The 20-35 degree left search is only a sampled near-extension range, not a certified mechanical travel limit.")
    caption("Structural solid CAD volume is 536.56 cm3. At an assumed PLA density of 1.24 g/cm3 this is 0.665 kg if solid; 50% infill does not simply halve the complete part mass because shells are unknown. Battery, servos and electronics are not included in that structural volume.")

    section(4, "Leg load and lifting margin")
    formula("Fz = M*g/2  (equal two-wheel support)\ntau_hip = Fz * dH/dq  (virtual work; q in radians)")
    figure("leg_height_torque.png", "Closed-linkage statics: force equilibrium and finite-difference virtual work agree across sampled poses. Model omits link self-weight distribution, joint friction and dynamic loads.", 210)
    table(["Robot mass scenario", "Each leg, static q=50 deg", "One-leg / 2x load"], [
        [f"{mass:.1f} kg", f"{0.7897393117 * mass:.2f} N m", f"{1.5794786234 * mass:.2f} N m"] for mass in [1, 1.5, 2, 2.5, 3, 4]
    ], [150, 180, WIDTH - 330])
    body("At q = 50 degrees, left hip height is <b>162.59 mm</b>. For 2 kg, predicted hip torque is <b>1.58 N m</b>, driven-knee pin load 40.86 N and support pin load 32.45 N. A linear interpolation of the supplier's 5 V / 6.8 V figures gives <b>2.28 N m at 6 V</b> [S3], but that is a stall reference, not a continuous holding rating.")
    body("A 3 kg two-leg static case already demands 2.37 N m at this pose; a 2 kg one-leg case demands 3.16 N m. Repeated height motion, friction and obstacle load transfer reduce margin further. A 40%-of-stall scenario supports only about 1.15 kg at q50; 40% is a sensitivity choice, not a published duty rating.")

    section(5, "Power: test before tuning harder")
    figure("power_budget.png", "Power scenarios, not measured currents. Wheel peak assumption: 2 A each; logic: 2.5 W; conversion efficiency: 85%.", 215)
    table(["Check", "Calculated / stated", "Interpretation"], [
        ["One leg buck", "2 A continuous [S4]", "12 W at 6 V; board cooling and wiring matter."],
        ["One leg stall screen", "2.29-3.00 A at 6 V", "Extrapolated from 2.6-3.4 A at 6.8 V [S3], assuming unchanged winding resistance."],
        ["Battery input", "5.89-6.81 A at 10.8 V", "Both leg stall scenarios + assumed wheel peaks; not a measured operating current."],
        ["Battery energy", "33.3 Wh nominal scenario", "3 Ah x assumed 11.1 V; pack/BMS discharge capability unknown [S8]."],
    ], [111, 143, WIDTH - 254])
    body("The LM2596 IC's 3 A label does not make every module a 3 A continuous supply [S5]. The installed leg modules may current-limit or heat under load. The wheel LTC3780 board has larger advertised limits [S6-S7], but its actual current-limit and undervoltage adjustments are unknown. <b>Neither is proven to be the wheel-stop cause.</b>")
    formula("C = delta_I * delta_t / delta_V\n3 A * 0.005 s / 0.3 V = 0.050 F = 50,000 uF")
    body("A 1,000 uF capacitor bridges only about 0.1 ms in that example. Bulk capacitance cannot repair an undersized supply. Record voltage at each servo connector and battery simultaneously with PWM/stop state; inspect common ground, wire/connector drops and regulator temperature. Do not raise voltage or blindly turn current-limit trimmers.")

    section(6, "Height-aware balance physics")
    figure("capture_envelope.png", "Ideal small-angle, point-mass velocity-capture screen. The 29.2 cm curve is the user's reported scenario, not reachable hip height certified by the supplied CAD. No finite motor acceleration, tire slip or servo lag is included.", 210)
    formula("h = H_ground_to_hip + CoM_above_hip - wheel_radius\nl_eff = h + I_com/(M*h)\ndelta_v = l_eff*theta_rate + sqrt(g*l_eff)*theta")
    body("The last expression is a simplified cart/pole instantaneous-velocity screen, not a promised recoverable angle. At a hypothetical 22.7 cm hip and CoM 3 cm above the hip, h = 0.2145 m. A 10 degree lean at zero angular rate gives <b>0.253 m/s</b>; a 5 degree lean with 40 degrees/s gives <b>0.277 m/s</b>.")
    body(f"An 85 mm wheel at the 50 RPM supplier reference travels only <b>{results['wheel_speed_baseline_m_s']:.3f} m/s</b> before loading [S1]. The manufacturer's alternate 56 RPM figure gives 0.249 m/s, with test voltage unclear [S2]. This indicates limited catch-speed margin, not proof that small-angle balance is impossible.")
    body("A taller point-mass pendulum has a slower angular growth rate, sqrt(g/l_eff), but needs more catch velocity for the same lean. Therefore <b>do not scale wheel speed or PID gains from height alone</b>. Account for height-specific trim, whole-robot CoM, inertia, linkage motion, measured actuator dynamics and saturation.")
    callout("500 Hz calculations are not 500 Hz actuation", "Saved V17 source computes at 2 ms and commands all four servo channels at 50 Hz. Faster browser rendering or a faster ESP calculation does not remove the 20 ms command frame and mechanical servo delay. Keep the hardware balance loop onboard, not over Wi-Fi.")

    section(7, "M4 pivots directly against PLA")
    figure("bolt_pivot_screen.png", "Nominal pin/bearing and clamp-friction sensitivity. Friction coefficient, clamp preload and engagement are assumptions; no material or bolt safety factor is established.", 210)
    m4 = joints["m4_at_3mm_engagement_scenario"]
    table(["2 kg / q50 example", "M4 result", "Assumption"], [
        ["Pin radial load", "40.86 N", "Driven-knee resultant, equal support"],
        ["Average single shear", f"{m4['single_shear_average_mpa']:.2f} MPa", "4 mm smooth shank; thread root ignored"],
        ["PLA projected bearing", f"{m4['projected_pla_bearing_pressure_mpa']:.2f} MPa", "3 mm engagement; p = F/(d*t)"],
        ["Bolt bending screen", f"{m4['cantilever_bending_stress_mpa_at_5mm_overhang_scenario']:.1f} MPa", "5 mm cantilever overhang; not measured"],
        ["Face friction torque", "0.30 N m at that pivot", "mu=0.3, clamp=100 N, 2 faces, radius=5 mm"],
    ], [151, 128, WIDTH - 279])
    body("M4 socket head specifies nominal thread size and drive style, not steel grade [S13]. The native knee bore is about <b>4.95 mm</b>; <b>if</b> M4 is used there, nominal diametral clearance is 0.95 mm (0.475 mm radial) before print tolerances. This can add backlash, while squeezing PLA faces can add stiction. Which occurs on your hardware needs measurement.")
    body("Use a dimensioned smooth pivot shoulder/pin with a sleeve/bushing or bearing and a spacer/locking arrangement that retains the joint without pinching rotating PLA. Avoid threads rubbing directly in the bearing plane. Check bore/wall geometry first; do not simply leave nuts loose. Bearing wear depends on load, shaft, motion and temperature [S14]; engineered bearing-polymer data is not PLA strength data.")
    caption("Inspect only with servo power isolated and body supported; never force a gearbox. Passive-pivot friction must be mapped through the linkage Jacobian, not blindly added 1:1 to hip torque. No tightening torque is specified for this unknown PLA joint.")

    section(8, "Solved native-link solid FEA")
    figure("upper_link_fea.png", "Left upper-link STEP solid, distributed knee-bore traction and clamped hip bores. Deformed shape is exaggerated 25x; surface stress colors are clipped at p99 to show the bulk field.", 345)
    table(["Mesh size", "Nodes / tetrahedra", "Max deflection", "Volume p95 stress"], [
        [f"{mesh['mesh_size_mm']:.1f} mm", f"{mesh['nodes']:,} / {mesh['tetrahedra']:,}", f"{mesh['max_displacement_mm']:.3f} mm", f"{mesh['von_mises_volume_p95_mpa']:.3f} MPa"] for mesh in fea["meshes"]
    ], [91, 163, 122, WIDTH - 376])
    body("Linear tetrahedral elasticity uses <b>E = 1.5 GPa, nu = 0.35</b>, assumed homogeneous/isotropic, under the 2 kg q50 pin load. Fine-mesh maximum deflection is <b>0.468 mm</b>; E = 0.8-2.6 GPa gives 0.877-0.270 mm. This is not a resolved 50%-infill or layer-anisotropy model.")
    body("Fine/coarse changes: displacement 0.70%, volume p95 stress 0.42%, but peak stress <b>13.45%</b>. The 4.89 MPa hip-clamp peak is <b>not converged</b>. Force balance, moment balance, strain patch and energy checks passed; mesh volume differs from the STEP solid by 0.004%.")
    caption("No bolt/contact/preload, creep, fatigue, impact, encoder-housing analysis or material coupon strength. Consequently no strength factor of safety or hardware structural approval is claimed [S13, S16].")

    section(9, "Sensors and why wheels stop")
    body("Two hip encoders plus the MPU can support leg-position control, body tilt/rate estimation and a kinematic height estimate. They do <b>not</b> measure wheel travel or base velocity. Add true wheel feedback before claiming an encoder-based wheel-speed/position outer loop. MPU accelerometer double integration is not a reliable substitute for that feedback.")
    table(["Observation during fault", "Investigate", "Evidence to record"], [
        ["PWM returns to neutral", "Disarm, fall cutoff, sensor gate, watchdog or command ownership", "Stop reason, tilt, IMU/encoder health, owner and uptime"],
        ["PWM remains commanded; 6 V drops", "Regulator limit, battery/BMS, connector or wiring drop", "Servo connector voltage + converter input + pack current"],
        ["PWM and rail remain good", "Mechanical bind, neutral/direction map or servo protection", "Command pulse, shaft motion, temperature, joint freedom"],
        ["Controller uptime resets", "Logic/pack brownout or reset cause", "Logic rail, reset log and pack voltage"],
    ], [156, 175, WIDTH - 331])
    sub("Saved evidence is not a live diagnosis")
    body("Today's saved V17 capture reports disarmed/stand-off, 1500 us outputs, missing height calibration and encoder field warnings. The 22.7 cm fallback is <b>not measured height</b>. Its manual-stop reason was logged after an intentional stop; it does not establish why the robot stopped during an earlier fall.")
    body("AS5600 status has separate magnet-detected, too-weak and too-strong flags [S11]. Magnet detected alone is insufficient. Check status, AGC and magnitude across full travel; verify coaxial diametric magnet alignment and signal continuity. One count is 0.0879 degrees, which is resolution, <b>not guaranteed absolute accuracy</b>.")
    callout("Do not bypass the failure", "Fix weak fields, mechanical slip and incomplete calibration before powered height moves. Do not suppress the encoder gate, raise the fall cutoff to hide stops, or recapture zero while the body is moving. Verify IMU axes/signs with servo power off; keep visual pitch correction separate from changing the hardware controller sign.")

    section(10, "Controller and commissioning plan")
    drawing = Drawing(WIDTH, 126)
    boxes = [(0, 69, 153, "ONBOARD ESTIMATION", "IMU + hip angles"), (178, 69, 153, "BOUNDED CONTROL", "Pitch + leg position"), (356, 69, 155, "SERVO INTERFACE", "50 Hz mapped pulses")]
    for horizontal, vertical, width, title, detail in boxes:
        drawing.add(Rect(horizontal, vertical, width, 48, fillColor=PALE, strokeColor=TEAL, rx=5, ry=5))
        drawing.add(String(horizontal + 9, vertical + 29, title, fontName="TalosBold", fontSize=8.8, fillColor=TEAL))
        drawing.add(String(horizontal + 9, vertical + 13, detail, fontName="Talos", fontSize=9.2, fillColor=INK))
    drawing.add(Line(153, 92, 178, 92, strokeColor=TEAL))
    drawing.add(Line(331, 92, 356, 92, strokeColor=TEAL))
    drawing.add(String(0, 37, "Laptop: visualization, telemetry, parameter fitting and OFFLINE simulation/training.", fontName="Talos", fontSize=10, fillColor=INK))
    drawing.add(String(0, 17, "Not the real-time balance loop. Wheel-speed outer loop needs actual wheel feedback.", fontName="Talos", fontSize=10, fillColor=GREY))
    story.append(drawing)
    table(["Stage", "Change / measure", "Acceptance before advancing"], [
        ["A / preserve", "Archive identified working leg-hold source/calibration; keep multi-Wi-Fi and safety settings.", "Known source, build and flash identity; no accidental auto-arm."],
        ["B / mechanics + sensing", "Correct pivot fit, free motion, magnet health and signs.", "Both hip readings track link motion throughout permitted travel."],
        ["C / supported height", "Calibrate individual sides; rate-limit angle targets and use anti-windup.", "Small commanded steps track in both directions without extension creep, rail collapse or overheating."],
        ["D / low balance", "Fixed low posture, measured wheel neutral/deadband/lag; log inner pitch response.", "Catch-frame tests recover small disturbances without repeated saturation or stops."],
        ["E / coordinated height", "Height-specific trim/feedforward; bounded differential roll correction.", "Slow height changes retain pitch/roll control and stay within measured travel/load limits."],
    ], [87, 219, WIDTH - 306])
    body("Control each continuous leg servo as an encoder-closed position loop commanding <b>speed</b>, not an absolute hobby-servo angle. Allow only small target-centered balance assistance; reset/limit integrators and stop on timeout or sensor fault. Pause height transitions when pitch/rate, saturation or power margin is inadequate.")
    body("The neutral/drive mapping and gravity feedforward must be measured under load. Later use a slower velocity loop to command a bounded pitch target, plus a faster local pitch loop. Height-based gain scheduling is a tuning aid, not a replacement for correcting a wrong sign, loose pivot or weak supply.")

    section(11, "Executable simulation: actual results")
    thumbnails = [image_fit(figures / f"simulation_h{label}.png", width=WIDTH / 3 - 6, height=110) for label in [150, 180, 210]]
    story.append(Table([thumbnails], colWidths=[WIDTH / 3] * 3))
    caption("Initial left driven-hip heights 150 / 180 / 210 mm. Right angles differ because the native hip offsets differ. These are kinematic pose checks, not stable physical tests.")
    table(["Offline check", "Result", "Limit"], [
        ["Model compile", "4 velocity-servo actuators", "Closed four-bars, CAD visuals; primitive contacts [S15]."],
        ["Initial wheel contact", "42.50 mm axle height both sides", "Three generated poses; closure errors under 2e-11 m."],
        ["Gymnasium checker", "Passed; 21 observations / 4 actions", "API/finite-state validation, not control success."],
        ["Height command / slew", "Passed at 150, 180, 210 mm", "Goal and 0.2 rad/s target limit only; not achieved height while balancing."],
        ["Untuned baseline", "5 / 5 episodes fell in 0.58-0.64 s", "Explicit failure result; no trained policy."],
    ], [132, 171, WIDTH - 303])
    traces = defaultdict(list)
    with (output / "simulation" / "baseline_trace.csv").open(encoding="utf-8") as stream:
        for record in csv.DictReader(stream):
            traces[int(record["seed"])].append((float(record["time_s"]), float(record["pitch_deg"])))
    plot, axis = plt.subplots(figsize=(9.5, 2.6))
    for seed, samples in traces.items():
        axis.plot([sample[0] for sample in samples], [sample[1] for sample in samples], color=COLORS[seed], label=f"seed {seed}")
    axis.axhline(35, color="#999999", linestyle="--", linewidth=1)
    axis.set(xlabel="Simulated time (s)", ylabel="Pitch truth (deg)", title="Untuned diagnostic baseline - failure, not trained balance")
    axis.grid(alpha=0.15)
    axis.legend(fontsize=7, ncol=5, loc="upper left")
    plot.tight_layout()
    finish_figure(plot, figures / "baseline_failures.png")
    figure("baseline_failures.png", "Ground-truth pitch is used for evaluation, not provided directly as simulator truth to the actor.", 160)
    body("Dynamic closure residual in the five episodes stayed below 0.066 mm. Three randomized smoke tests also produced finite observations. These numerical checks do not identify the real servo, joint friction, CoM, rail behavior or terrain contact. <b>Simulation falls do not prove the real robot cannot balance.</b>")

    section(12, "Training, obstacles and self-righting")
    body("Yes, this robot can be represented and trained offline, but identification comes first. The starter uses four normalized continuous-servo velocity actions, 2 ms physics, 20 ms command frames, assumed deadband/lag and bounded force. Actor inputs are the available IMU/hip channels, previous actions, target angles and commands; no fake wheel encoder is added.")
    table(["Training stage", "Needed work / evaluation"], [
        ["1 / identify", "Weigh links/body and locate CoM; fit each servo's pulse-to-speed, loaded response, lag, neutral and reversal; measure pivot stiction/backlash and supply sag."],
        ["2 / fixed posture", "Train/evaluate low-height balance across randomized measured uncertainties. Use held-out seeds, perturbations and sustained survival, not a single attractive clip."],
        ["3 / variable height", "Add slow target transitions and asymmetric ground; score actual angle tracking, pitch/roll, saturation and actuator limits."],
        ["4 / steps + recovery", "Add validated contact geometry, self-collision, actuator/power limits and safe contact sequences before any sim-to-real deployment."],
    ], [119, WIDTH - 119])
    sub("A small obstacle is already a different load case")
    formula("tau_wheel = (M*g/2) * sqrt(2*r*s - s*s)\nIdeal rigid corner, shared vertical load, 0 < s < r")
    table(["Step height", "2 kg torque / wheel", "What this omits"], [
        ["10 mm", "0.269 N m", "Tire compression, friction and chassis clearance"],
        ["20 mm", "0.354 N m", "One-wheel contact and dynamic load transfer"],
        ["30 mm", "0.398 N m", "Leg lifting torque and real torque-speed behavior"],
    ], [108, 169, WIDTH - 277])
    body("One-wheel support can double the ideal wheel torque and leg load. No obstacle height is approved from this screen. Lying-down automatic startup additionally needs a demonstrated body-ground contact sequence, clearance and torque margin; an IMU alone does not make self-righting possible.")
    callout("No training or deployment claim", "The optional PPO script is provided but PyTorch/Stable-Baselines3 were not installed or run for this assessment. Current model masses, 60/80 ms lags and dry-joint behavior are not identified. No learned weights or hardware interface is included.")

    section(13, "What to borrow from the videos")
    body("Your pasted summaries are useful references, not independently verified transcripts. Video titles/creators and selected primary project pages/repositories were checked; the complete videos were not watched in this assessment. The four projects use different actuation, sensing or joint counts.")
    table(["Reference video", "Applicable lesson", "Do not transfer blindly"], [
        ["Tazer: Building a Robot so I don't get kicked out of Uni (QZeO-wh_1HY)", "Separate fast pitch stabilization and slower motion regulation; characterize height-dependent pitch trim.", "Reported 48 V/CAN/QDD architecture and gain values are not TALOS PWM-servo settings."],
        ["Tazer: My Robot almost got me Kicked out of Uni (gqnW9qBCHnM)", "Instrument motor/control faults and improve terrain behavior only after reliable base balance.", "CAN pacing or material-specific traction claims do not justify delaying this loop or replacing rubber tires without tests."],
        ["Kayden Knapik: I Built Disney's Star Wars Robot (r5bIgLKbuCI)", "Borrow identification, randomized simulation, staged learning and measured sim-to-real validation [S17-S18].", "Multi-joint footed robot policies and strong torque actuators do not map directly to four continuous servos."],
        ["Build Some Stuff: I built a self-balancing robot from scratch (K1lzzVGCzAQ)", "Four-bar packaging and careful subsystem commissioning are relevant [S19].", "Different motors/encoders and position servos cannot supply unmodified TALOS gains or actions."],
    ], [170, 170, WIDTH - 340])
    body("<b>Corrections to the supplied summary:</b> TALOS AS5600s measure legs, not wheels. An LQR problem on another machine does not prove LQR is universally unsuitable. TPU or silicone traction depends on formulation, tread, load and surface. MjLab offers an Isaac Lab-style API on MuJoCo Warp; it is not automatically Isaac Sim and MuJoCo running together [S18].")
    body("For TALOS, prioritize measured continuous-servo dynamics, encoder calibration, correct four-bar geometry and supply limits. Upkie is another useful open-source wheel-legged reference [S20], but is not hardware-equivalent. Preserve the existing fixed-height hold while adding testable, bounded height commands.")

    section(14, "Reproduce and close the gaps")
    sub("What is in the package")
    table(["Artifact", "Reproducible scope"], [
        ["native-cad.json + STEP + GLB", "Read-only native transforms, dimensions and solid link geometry; original source hashes unchanged."],
        ["analyse.py + joints.py", "Kinematics, static loads, balance/power screens, M4 clearance and joint-load sensitivities; CSV and JSON outputs."],
        ["fea.py + mesh/solution files", "Three solved native upper-link meshes, residual/patch/energy checks and stiffness sensitivity."],
        ["simulation.py + talos_env.py", "Four-actuator closed-linkage model, three pose checks, API/finite-state validation and transparent baseline failures."],
        ["train_offline.py", "Optional PPO training entry; unrun, no real robot interface."],
        ["report.py + README", "This report, evidence captions, references and offline commands."],
    ], [172, WIDTH - 172])
    sub("Measurements that unlock a credible next revision")
    body("<b>Mass / CoM:</b> weigh the complete robot and major moving parts. Measure balance-point offsets safely with power isolated; do not infer whole-robot CoM from a 50% infill setting or the head's geometric center.")
    body("<b>Geometry / joints:</b> measure ground-to-driven-hip and head-top at both height endpoints; verify pivot center distances, bore/shank diameters, sleeve length, lateral play, thread position and smooth movement. Identify which pivots really use M4.")
    body("<b>Actuation / power:</b> log pulse, angle/speed, direction, supply voltage/current and temperature. Use a secured fixture, small bounded moves, immediate cutoff and no prolonged stalls. Determine regulator/BMS current limits from actual documentation and measurements.")
    body("<b>Firmware identity:</b> saved service artifacts show V17 uploads today, but do not identify the earlier source from the verbal description. Preserve the pre-upload flash backup and identify the matching earlier project before changing its known-good hold behavior. No flash restoration or COM8 access was performed here.")
    caption("For reproducibility, run the commands in tools/cae/README.md from the extracted root. Original SolidWorks is needed only for re-extraction; normal analysis, FEA and simulation use bundled geometry. Re-run the default model and checks after changing assumptions.")
    callout("Engineering decision", "Do not attempt unattended self-standing/climbing yet. The next defensible milestone is healthy encoders and smooth, accurately tracking supported leg-height changes, followed by recoverable low-height wheel balance under a catch frame.")

    section(15, "Sources and evidence boundaries")
    caption("Web references checked 11 Oct 2026. Supplier ratings are not measured performance. No long quotations are used. Native CAD, saved logs and user statements are separately labelled throughout.")
    for source in sources:
        url = escape(source["url"], quote=True)
        title = escape(source["title"])
        scope = escape(source["scope"])
        story.append(paragraph(f'<b>[{source["id"]}]</b> <link href="{url}" color="#12665b">{title}</link><br/>{scope}', "RefTalos"))

    destination.parent.mkdir(parents=True, exist_ok=True)
    document = AssessmentDocument(destination, title="TALOS - CAD-informed CAE and offline simulation", author="TALOS Engineering Assessment", subject="Preliminary mechanics, power, controls and simulation with explicit evidence limits")
    document.build(story)
    reader = PdfReader(str(destination))
    layout = {"pdf": str(destination), "page_count": len(reader.pages), "section_start_pages": document.sections,
              "body_font_pt": 11, "figures": len(list(figures.glob("*.png"))),
              "trained_policy": False, "hardware_validated": False, "raster_visual_qa": "pending"}
    save_json(output / "report_layout.json", layout)
    shutil.copy2(Path(__file__).with_name("sources.json"), output / "sources.json")
    print(json.dumps(layout, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--pdf", type=Path, default=ROOT / "output" / "pdf" / "TALOS_CAE_Assessment.pdf")
    arguments = parser.parse_args()
    report(arguments.output, arguments.pdf)

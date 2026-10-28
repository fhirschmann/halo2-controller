// opp parts library (skill openscad-print-project): building blocks from Fabian's projects.
// Copy into a project with `opp lib` and `use <lib/opp_parts.scad>;` (functions and modules, no top-level geometry).
// Units mm, Z up. Status per block:
//   [proven]  printed and in use, or Fabian's measured standard (reference/hardware.md)
//   [draft]   designed in a project, not yet proven on the real object: check fit with a coupon first
// Every block renders as a closed solid on its own (tests/test_library.py).

// ---------- rules of thumb (0.4 nozzle, PETG) [proven] ----------
function opp_wall_min() = 1.2;          // three lines
function opp_web_min() = 1.1;           // web between engravings
function opp_inlay_line_min() = 0.8;    // narrowest colour inlay line
function opp_slide_clearance() = 0.25;  // per side, 0.2-0.3

// ---------- 2D helpers ----------
// Circle with vertices at 0/90/180/270 degrees, so hulls of them have exactly flat tops and sides
function opp_fn4(r, fs = 0.4) = max(16, 4 * ceil(2 * PI * r / fs / 4));

// Rounded rectangle from (x0, y0) to (x1, y1): radius rl on the top left, rr on the top right, square bottom
// [proven: ua-hooks profile helper]
module opp_rrect(x0, y0, x1, y1, rl, rr) hull() {
    translate([x0 + rl, y1 - rl]) circle(r = rl, $fn = opp_fn4(rl));
    translate([x1 - rr, y1 - rr]) circle(r = rr, $fn = opp_fn4(rr));
    translate([x0, y0]) square([0.01, 0.01]);
    translate([x1 - 0.01, y0]) square([0.01, 0.01]);
}

// Rounded square w x d with corner radius r
module opp_rounded_square(w, d, r, center = true)
    translate(center ? [0, 0] : [w / 2, d / 2]) offset(r = r) offset(delta = -r) square([w, d], center = true);

// Opens features thinner than w (glyph spikes of small text) so inlay lines stay printable [proven: template]
module opp_min_width(w = 0.8) offset(r = w / 2, $fs = 0.1) offset(delta = -w / 2) children();

// 2D children extruded h high with 45 degree chamfers on both flat faces, in whole layer steps
// (bed side at z = 0: no elephant foot, no sharp edge on the visible face) [draft: ua-hooks]
module opp_chamfered_prism(h, c = 0.6, layer = 0.2) {
    k = round(c / layer);
    assert(abs(k * layer - c) < 0.001, "chamfer must be whole layers");
    for (i = [0 : k - 1]) {
        translate([0, 0, i * layer]) linear_extrude(layer + 0.01) offset(delta = -(c - i * layer)) children();
        translate([0, 0, h - (i + 1) * layer - 0.01]) linear_extrude(layer + 0.01) offset(delta = -(c - i * layer)) children();
    }
    translate([0, 0, c - 0.01]) linear_extrude(h - 2 * c + 0.02) children();
}

// ---------- hardware [proven: reference/hardware.md] ----------
// Ruthex RX and short series: [hole d3, minimum wall w, insert length L]; blind hole >= L + 1
function opp_ruthex(size) =
    size == "M2"   ? [3.2, 1.3, 4.0] :
    size == "M2.5" ? [4.0, 1.6, 5.7] :
    size == "M3"   ? [4.0, 1.6, 5.7] :
    size == "M3S"  ? [4.0, 1.6, 4.0] :
    size == "M4"   ? [5.6, 2.1, 8.1] :
    size == "M4S"  ? [5.6, 2.1, 6.0] :
    size == "M5"   ? [6.4, 2.6, 9.5] :
    size == "M5S"  ? [6.4, 2.6, 7.0] :
    size == "M6"   ? [8.0, 3.3, 12.7] : undef;

// Cutter for a heat-set insert pocket: from z = 0 down into the material, blind depth L + extra
module opp_insert_pocket(size = "M3", extra = 1) {
    s = opp_ruthex(size);
    assert(s != undef, str("unknown insert ", size));
    translate([0, 0, -(s[2] + extra)]) cylinder(d = s[0], h = s[2] + extra + 0.01, $fn = 32);
}

// Boss for an insert: z = 0 .. h, outer diameter hole + 2 x minimum wall, pocket from the top
module opp_insert_boss(size = "M3", h = 8) {
    s = opp_ruthex(size);
    assert(h >= s[2] + 1.6, "boss shorter than insert + floor");
    difference() {
        cylinder(d = s[0] + 2 * s[1], h = h, $fn = 48);
        translate([0, 0, h]) opp_insert_pocket(size);
    }
}

// Pocket for a disc magnet d x h (Fabian's fit, 2026-09-28: d + 0.04, depth h + 0.2), from z = 0 down
module opp_magnet_pocket(d = 10, h = 3)
    translate([0, 0, -(h + 0.2)]) cylinder(d = d + 0.04, h = h + 0.2 + 0.01, $fn = 64);

// ISO 7380 button head screw from z = 0 down: head pocket depth `head` (Ø6.4 M3, Ø8.5 M4), clearance hole below
module opp_screw_pocket(size = "M3", length = 10, head = 2) {
    d = size == "M3" ? [3.4, 6.4] : size == "M4" ? [4.5, 8.5] : undef;
    assert(d != undef, str("unknown screw ", size));
    translate([0, 0, -head]) cylinder(d = d[1], h = head + 0.01, $fn = 48);
    translate([0, 0, -head - length]) cylinder(d = d[0], h = length + 0.02, $fn = 32);
}

// Pilot hole Ø2.1 for Fabian's 2.5 mm thermoplastic screws, from z = 0 down (keep >= 1.5 mm skin to a visible face)
module opp_thermoplast_pilot(depth = 8) translate([0, 0, -depth]) cylinder(d = 2.1, h = depth + 0.01, $fn = 24);

// ---------- patterns ----------
// Honeycomb holes for a w x h panel (centred), 8 mm across flats, 2 mm struts; pointy = vertex up, printable
// upright without bridges (60 degree flanks) [draft: besta-drawers, not printed yet]
module opp_honeycomb_2d(w, h, flat = 8, strut = 2, margin = 6, pointy = true) {
    p = flat + strut;
    r = flat / sqrt(3);
    if (pointy) {
        dv = p * sqrt(3) / 2;
        for (j = [0 : floor((h - 2 * margin - 2 * r) / dv)])
            let (y = -h / 2 + margin + r + j * dv, x0 = -w / 2 + margin + flat / 2 + (j % 2) * p / 2)
                for (x = [x0 : p : w / 2 - margin - flat / 2]) translate([x, y]) rotate(30) circle(r = r, $fn = 6);
    } else {
        dx = p * sqrt(3) / 2;
        for (i = [0 : floor((w - 2 * margin - 2 * r) / dx)])
            let (x = -w / 2 + margin + r + i * dx, y0 = -h / 2 + margin + flat / 2 + (i % 2) * p / 2)
                for (y = [y0 : p : h / 2 - margin - flat / 2]) translate([x, y]) circle(r = r, $fn = 6);
    }
}

// Honeycomb panel w x h x t, holes through [draft]
module opp_honeycomb_panel(w, h, t = 1.6, flat = 8, strut = 2, margin = 6, pointy = true)
    linear_extrude(t) difference() { square([w, h], center = true); opp_honeycomb_2d(w, h, flat, strut, margin, pointy); }

// ---------- Gridfinity [proven: gridfinity (IKEA BROR), standard bins fit] ----------
function opp_gf_ring(width, radius, z, segments = 12) =
    [for (corner = [0 : 3], step = [0 : segments])
        let (c = (width - 2 * radius) / 2, cx = corner == 0 || corner == 3 ? -c : c, cy = corner < 2 ? -c : c,
             angle = 180 + 90 * corner + 90 * step / segments)
        [cx + radius * cos(angle), cy + radius * sin(angle), z]];

// Closed loft through stations [z, width, corner radius] of rounded squares (exact 45 degree ramps)
module opp_gf_loft(stations, segments = 12) {
    n = 4 * (segments + 1);
    layers = len(stations);
    points = [for (s = stations) each opp_gf_ring(s[1], s[2], s[0], segments)];
    faces = concat([[for (i = [n - 1 : -1 : 0]) i]],
                   [for (j = [0 : layers - 2], i = [0 : n - 1]) [j * n + i, j * n + (i + 1) % n, (j + 1) * n + (i + 1) % n, (j + 1) * n + i]],
                   [[for (i = [0 : n - 1]) (layers - 1) * n + i]]);
    polyhedron(points = points, faces = [for (face = faces) [for (i = [len(face) - 1 : -1 : 0]) face[i]]], convexity = 10);
}

// One 42 mm cell pocket (standard 5 mm female profile), centred, from z = 0 up
module opp_gridfinity_cavity()
    opp_gf_loft([[-0.01, 36.3, 1.15], [0.35, 36.3, 1.15], [1.05, 37.7, 1.85], [2.85, 37.7, 1.85], [5, 42, 4], [5.01, 42, 4]]);

// Baseplate nx x ny cells, x = 0 .. nx * 42, y = 0 .. ny * 42, body height 4 (stops below the knife edge), open below
module opp_gridfinity_baseplate(nx, ny, height = 4) {
    assert(nx >= 1 && ny >= 1 && height > 2.85 && height <= 4, "whole cells, body 2.85..4 mm");
    difference() {
        cube([nx * 42, ny * 42, height]);
        for (x = [0 : nx - 1], y = [0 : ny - 1]) translate([(x + 0.5) * 42, (y + 0.5) * 42, 0]) opp_gridfinity_cavity();
    }
}

// Nominal bin foot seated at z = 0, a gauge for checks (not a print part)
module opp_gridfinity_bin_foot()
    opp_gf_loft([[0, 35.6, 0.8], [0.8, 37.2, 1.6], [2.6, 37.2, 1.6], [4.75, 41.5, 3.75]]);

// ---------- clips and hooks [draft: ua-hooks, coupons not printed yet] ----------
// Clip over a rounded-rectangle tube (2D profile, tube's inner face at x = 0, top at y = 0): band t over the
// tube with jaws j_in (inside) and j_out (outside) deep, a nose at each tip reaching `pinch` into the tube.
// pinch > 0 is the print shape, pinch 0 the installed one. Extrude along the tube; test with coupons first.
module opp_tube_clip_2d(tube_w, tube_h, r_in = 4, r_out = 12, t = 4, clr = 0.3, j_in = 6, j_out = 15,
                        pinch = 0.5, r_nose = 1.5) {
    module inside() translate([0, -clr]) offset(r = clr) opp_rrect(0, -tube_h - 30, tube_w, 0, r_in, r_out);
    difference() {
        offset(r = t) inside();
        inside();
        translate([-50, -tube_h - 100]) square([50 + tube_w / 2, tube_h + 100 - j_in]);
        translate([tube_w / 2, -tube_h - 100]) square([100, tube_h + 100 - j_out]);
    }
    translate([-clr - t / 2, -j_in]) circle(d = t);
    translate([tube_w + clr + t / 2, -j_out]) circle(d = t);
    translate([pinch - r_nose, -j_in + r_nose]) circle(r = r_nose);
    translate([tube_w - pinch + r_nose, -j_out + r_nose]) circle(r = r_nose);
}

// J hook profile: leg from y = 0 down `leg` long at x = 0 .. t, bend of inner radius r, tip `tip` long
module opp_j_hook_2d(leg = 60, r = 9, t = 5, tip = 16) {
    translate([0, -leg]) square([t, leg]);
    translate([t + r, -leg]) difference() { circle(r = r + t); circle(r = r); translate([-50, 0]) square(100); }
    translate([2 * r + t, -leg]) square([t, tip]);
    translate([2 * r + 1.5 * t, tip - leg]) circle(d = t);
}

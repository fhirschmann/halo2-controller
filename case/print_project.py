"""Computed settings and checks for the openscad-print-project tools (opp, pinned in requirements.txt).

The static settings are in opp.toml. This file runs after it and holds only checks(ctx).
"""


def checks(ctx):
    """Project-specific checks after export (see the package docs for the ctx helpers)."""
    m = ctx.metrics
    assert m["wall"] >= 1.2, "Walls below three perimeters"
    assert m["port_bar"] >= 1.2, "Web between the cut-outs below three lines"
    # The lid rests on the rim, the DevKit on the corner supports, the module hangs under the DevKit, the PD board
    # lies with its table side on the lid boss and rests, the M2 heads hold it from below
    contacts = ctx.contacts([("lid", "box", [0, 0, -1]), ("devkit", "box", [0, 0, -1]), ("bm5602", "devkit", [0, 0, 1]),
                             ("screws", "lid", [0, 0, -1]), ("kit_screws", "devkit", [0, 0, -1]),
                             ("pdboard", "lid", [0, 0, 1]), ("pd_screws", "pdboard", [0, 0, 1])])
    # Hold-down and plug forces: the four screws hold the DevKit, the front wall and the two M2 hold the PD board
    stops = ctx.stops([("devkit_up", "devkit", "kit_screws", [0, 0, 1], 0.3),
                       ("devkit_pull", "devkit", "box", [-1, 0, 0], m["kit_front"] + 0.3),
                       ("devkit_push", "devkit", "kit_screws", [1, 0, 0], m["kit_hole_cl"] + 0.3),
                       ("pd_pull", "pdboard", "box", [-1, 0, 0], m["pd_front"] + 0.3),
                       ("pd_push", "pdboard", "pd_screws", [1, 0, 0], m["pd_hole_cl"] + 0.3),
                       ("pd_drop", "pdboard", "pd_screws", [0, 0, -1], 0.3)])
    # Assembly (reversed): the lid with the PD board slides back out of the USB-C cut-out and lifts off; the DevKit
    # with the module slides back out of the micro-USB cut-out and lifts out; without its screws the PD board drops off;
    # the plugs pull out to the front
    paths = ctx.paths([("lid_off", ["lid", "pdboard", "pd_screws"], ["box", "devkit", "bm5602", "kit_screws"],
                        [([1, 0, 0], m["lid_slide"] + 0.1, 0.1), ([0, 0, 1], 25, 0.5)]),
                       ("devkit_out", ["devkit", "bm5602"], "box",
                        [([1, 0, 0], m["kit_slide"] + 0.1, 0.1), ([0, 0, 1], 25, 0.5)]),
                       ("pd_out", "pdboard", "lid", [0, 0, -1], 10, 0.5),
                       ("plug_out", "usb_plug", ["box", "lid", "devkit", "pdboard"], [-1, 0, 0], 15, 0.5),
                       ("usbc_out", "usbc_plug", ["box", "lid", "devkit", "pdboard"], [-1, 0, 0], 15, 0.5)])
    # Ruthex RX-M3x5.7 pockets: hole 4.0, datasheet wall 1.6, blind depth L + 1
    inserts = ctx.insert_probes([("box", [p[0], p[1], m["box_h"]], [0, 0, -1], m["pocket_depth"], 4.0, 1.6)
                                 for p in m["boss_xy"]])
    assert 5.7 <= m["screw_engage"] <= m["pocket_depth"] - 0.2, "M3 x 8 does not fit the insert pocket"
    ctx.open_items.append("PD board holes from the scan (+-2.5 mm, 2.2 mm from the far end): the screws locate the board, "
                          "measure the hole centres to the USB-C front with calipers")
    ctx.open_items.append("M2 x 6 assumed from the VooGenzek set; pilot 1.6 mm untested in PETG")
    ctx.open_items.append("PD board heights estimated (PCB 1.6, receptacle 3.2, parts 1.5): measure before printing")
    ctx.open_items.append("Stack 12 mm measured on the first board, split estimated: measure the DOIT stack with the module glued")
    ctx.open_items.append("DOIT board from the scan: holes 46.73 x 23.5 apart, width 28.1 an upper bound: check with calipers")
    return dict(contact_volumes_mm3=contacts, stops=stops, sampled_paths=paths, inserts=inserts)

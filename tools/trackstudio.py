#!/usr/bin/env python3
"""SMK Track Studio: draw a course, place its objects, pick its theme, and
have the AI's data generated and checked - no manual steps
(docs/TRACKS.md).

    python3 tools/trackstudio.py [tracks/mine]

Paint roles on the map with the brush, place objects by clicking, draw
the start line, press Build.  The Build compiles the theme's tiles,
generates the sector map and racing line, runs the validator and writes
the package the game loads; Race asks the ROM's own AI field to lap it;
Play starts the game on it.  Standard library only (tkinter).
"""
from __future__ import annotations
import os, queue, sys, threading, tkinter as tk
from tkinter import ttk, filedialog, messagebox
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool import project as PJ
from smktool import surface as S

TILE = 8                      # pixels per tile in the picture
ROLE_TOOLS = [("Road", "ROAD"), ("Off-road", "OFF"), ("Wall", "WALL"), ("Water (wade)", "WATER"),
              ("Void / lava", "HAZARD"), ("Breakable block", "BLOCK")]
MARKER_TOOLS = [("Item box", "box"), ("Coin", "coin"), ("Coin scatter", "coins"), ("Oil slick", "oil"),
                ("Boost pad", "pad"), ("Ramp (road runs up/down)", "ramp"), ("Ramp (road runs left/right)", "rampv"),
                ("Obstacle (the theme's)", "entity")]
ROW_COLOUR = {0: "#ff4040", 1: "#ffa040", 2: "#ffff40", 3: "#40ff60"}


def hexrgb(c):
    return "#%02x%02x%02x" % tuple(c)


class Studio:
    def __init__(self, root: tk.Tk, path: str | None):
        self.root = root
        root.title("SMK Track Studio")
        self.project = PJ.Project.new("MY COURSE", 1)
        self.zoom = 1.0
        self.view = tk.StringVar(value="roles")
        self.show_sectors = tk.BooleanVar(value=True)
        self.show_line = tk.BooleanVar(value=True)
        self.show_grid = tk.BooleanVar(value=True)
        self.tool = tk.StringVar(value="ROAD")
        self.brush = tk.IntVar(value=3)
        self.dirty_since_build = True
        self.unsaved = False
        self.undo_stack: list = []
        self.redo_stack: list = []
        self.selected_wp = -1
        self.busy = False
        self.q: queue.Queue = queue.Queue()
        self.photo = None
        self.base_rgb = None
        self._build_ui()
        if path:
            self.open_dir(path)
        else:
            self.redraw(full=True)
        root.after(100, self._poll)
        root.protocol("WM_DELETE_WINDOW", self.quit)

    # ---- layout -------------------------------------------------------------
    def _build_ui(self):
        r = self.root
        menu = tk.Menu(r)
        fm = tk.Menu(menu, tearoff=0)
        fm.add_command(label="New course...", command=self.new_course, accelerator="Ctrl+N")
        fm.add_command(label="Open package...", command=self.open_dialog, accelerator="Ctrl+O")
        fm.add_command(label="Save", command=self.save, accelerator="Ctrl+S")
        fm.add_command(label="Save as...", command=self.save_as)
        fm.add_separator()
        fm.add_command(label="Export picture (PNG)...", command=self.export_png)
        fm.add_separator()
        fm.add_command(label="Quit", command=self.quit)
        menu.add_cascade(label="File", menu=fm)
        em = tk.Menu(menu, tearoff=0)
        em.add_command(label="Undo", command=self.undo, accelerator="Ctrl+Z")
        em.add_command(label="Redo", command=self.redo, accelerator="Ctrl+Y")
        em.add_separator()
        em.add_command(label="Start again from the oval", command=lambda: self.reset(True))
        em.add_command(label="Start again from a blank map", command=lambda: self.reset(False))
        menu.add_cascade(label="Edit", menu=em)
        vm = tk.Menu(menu, tearoff=0)
        vm.add_radiobutton(label="Roles (what you drew)", variable=self.view, value="roles", command=lambda: self.redraw(full=True))
        vm.add_radiobutton(label="Tiles (what the game draws)", variable=self.view, value="tiles", command=lambda: self.redraw(full=True))
        vm.add_separator()
        vm.add_checkbutton(label="Sectors", variable=self.show_sectors, command=lambda: self.redraw(full=True))
        vm.add_checkbutton(label="Racing line and waypoints", variable=self.show_line, command=self.draw_overlays)
        vm.add_checkbutton(label="Finish strip and grid", variable=self.show_grid, command=self.draw_overlays)
        vm.add_separator()
        vm.add_command(label="Zoom in", command=lambda: self.set_zoom(self.zoom * 2), accelerator="+")
        vm.add_command(label="Zoom out", command=lambda: self.set_zoom(self.zoom / 2), accelerator="-")
        menu.add_cascade(label="View", menu=vm)
        hm = tk.Menu(menu, tearoff=0)
        hm.add_command(label="How it works", command=self.help)
        menu.add_cascade(label="Help", menu=hm)
        r.config(menu=menu)

        outer = ttk.Frame(r)
        outer.pack(fill="both", expand=True)

        # left: the course and the tools
        left = ttk.Frame(outer, padding=6)
        left.pack(side="left", fill="y")
        ttk.Label(left, text="Course", font=("TkDefaultFont", 10, "bold")).pack(anchor="w")
        self.name_var = tk.StringVar(value=self.project.name)
        ttk.Entry(left, textvariable=self.name_var, width=22).pack(anchor="w", pady=(0, 4))
        ttk.Label(left, text="Theme (tiles, music, creatures)").pack(anchor="w")
        self.theme_var = tk.StringVar(value=PJ.THEME_NAMES[self.project.theme])
        cb = ttk.Combobox(left, textvariable=self.theme_var, values=PJ.THEME_NAMES, state="readonly", width=20)
        cb.pack(anchor="w", pady=(0, 4))
        cb.bind("<<ComboboxSelected>>", lambda e: self.on_theme())
        f = ttk.Frame(left); f.pack(anchor="w", pady=(0, 6))
        ttk.Label(f, text="Item block").pack(side="left")
        self.items_var = tk.IntVar(value=self.project.items)
        ttk.Spinbox(f, from_=0, to=7, textvariable=self.items_var, width=3).pack(side="left", padx=4)

        ttk.Label(left, text="Paint", font=("TkDefaultFont", 10, "bold")).pack(anchor="w", pady=(6, 0))
        for label, role in ROLE_TOOLS:
            fr = ttk.Frame(left); fr.pack(anchor="w")
            sw = tk.Label(fr, width=2, bg=hexrgb(PJ.ROLE_RGB[role])); sw.pack(side="left", padx=(0, 4))
            ttk.Radiobutton(fr, text=label, variable=self.tool, value=role).pack(side="left")
        fr = ttk.Frame(left); fr.pack(anchor="w")
        tk.Label(fr, width=2, bg=hexrgb(PJ.ROLE_RGB["LINE"])).pack(side="left", padx=(0, 4))
        ttk.Radiobutton(fr, text="Start line (click the road)", variable=self.tool, value="LINE").pack(side="left")
        f = ttk.Frame(left); f.pack(anchor="w", pady=(2, 6))
        ttk.Label(f, text="Brush").pack(side="left")
        ttk.Scale(f, from_=1, to=12, variable=self.brush, orient="horizontal", length=110).pack(side="left", padx=4)

        ttk.Label(left, text="Place", font=("TkDefaultFont", 10, "bold")).pack(anchor="w")
        for label, fam in MARKER_TOOLS:
            fr = ttk.Frame(left); fr.pack(anchor="w")
            tk.Label(fr, width=2, bg=hexrgb(PJ.MARKER_RGB[fam])).pack(side="left", padx=(0, 4))
            ttk.Radiobutton(fr, text=label, variable=self.tool, value="M:" + fam).pack(side="left")
        ttk.Label(left, text="right click removes an object", foreground="#666").pack(anchor="w")
        ttk.Label(left, text="Tune", font=("TkDefaultFont", 10, "bold")).pack(anchor="w", pady=(6, 0))
        ttk.Radiobutton(left, text="Waypoints (drag; keys 0-3 set the AI speed row)", variable=self.tool, value="WP").pack(anchor="w")

        # centre: the map
        centre = ttk.Frame(outer)
        centre.pack(side="left", fill="both", expand=True)
        self.canvas = tk.Canvas(centre, bg="#202020", width=1024, height=1024, highlightthickness=0)
        hs = ttk.Scrollbar(centre, orient="horizontal", command=self.canvas.xview)
        vs = ttk.Scrollbar(centre, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=hs.set, yscrollcommand=vs.set)
        vs.pack(side="right", fill="y"); hs.pack(side="bottom", fill="x")
        self.canvas.pack(side="left", fill="both", expand=True)
        c = self.canvas
        c.bind("<ButtonPress-1>", self.on_press)
        c.bind("<B1-Motion>", self.on_drag)
        c.bind("<ButtonRelease-1>", self.on_release)
        c.bind("<ButtonPress-3>", self.on_right)
        c.bind("<B3-Motion>", self.on_right)
        c.bind("<Motion>", self.on_move)
        c.bind("<MouseWheel>", self.on_wheel)
        c.bind("<Button-4>", lambda e: self.canvas.yview_scroll(-3, "units"))
        c.bind("<Button-5>", lambda e: self.canvas.yview_scroll(3, "units"))
        r.bind("<Control-z>", lambda e: self.undo())
        r.bind("<Control-y>", lambda e: self.redo())
        r.bind("<Control-s>", lambda e: self.save())
        r.bind("<Control-o>", lambda e: self.open_dialog())
        r.bind("<Control-n>", lambda e: self.new_course())
        r.bind("<Key-plus>", lambda e: self.set_zoom(self.zoom * 2))
        r.bind("<Key-minus>", lambda e: self.set_zoom(self.zoom / 2))
        for k in "0123":
            r.bind(k, lambda e, k=int(k): self.set_row(k))

        # right: build, check, results
        right = ttk.Frame(outer, padding=6, width=300)
        right.pack(side="right", fill="y")
        right.pack_propagate(False)
        ttk.Label(right, text="Check", font=("TkDefaultFont", 10, "bold")).pack(anchor="w")
        self.b_build = ttk.Button(right, text="Build and validate", command=self.build)
        self.b_build.pack(fill="x", pady=2)
        self.b_race = ttk.Button(right, text="Race the AI on it (50/100/150cc)", command=self.race)
        self.b_race.pack(fill="x", pady=2)
        self.b_play = ttk.Button(right, text="Play it", command=lambda: self.play(False))
        self.b_play.pack(fill="x", pady=2)
        self.b_tt = ttk.Button(right, text="Time trial on it", command=lambda: self.play(True))
        self.b_tt.pack(fill="x", pady=2)
        self.progress = ttk.Label(right, text="", foreground="#246")
        self.progress.pack(anchor="w", pady=(4, 2))
        ttk.Label(right, text="Results", font=("TkDefaultFont", 10, "bold")).pack(anchor="w")
        self.results = tk.Text(right, width=38, height=40, wrap="word", state="disabled", font=("TkFixedFont", 9))
        self.results.pack(fill="both", expand=True)
        self.results.tag_config("bad", foreground="#b00")
        self.results.tag_config("good", foreground="#070")
        self.results.tag_config("note", foreground="#555")
        self.results.tag_config("head", font=("TkDefaultFont", 9, "bold"))
        self.status = ttk.Label(r, text="", anchor="w", relief="sunken")
        self.status.pack(side="bottom", fill="x")

    # ---- results panel -------------------------------------------------------
    def say(self, lines):
        self.results.configure(state="normal")
        self.results.delete("1.0", "end")
        for text, tag in lines:
            self.results.insert("end", text + "\n", tag)
        self.results.configure(state="disabled")

    def show_verdict(self, extra=None):
        pr = self.project
        lines = []
        if pr.package:
            lines.append(("Built: %d sectors, %d waypoints, %d objects, %d obstacles" % (
                pr.package.sectors, pr.package.sectors, len(pr.package.stamps), len(pr.package.ents)), "head"))
            lines.append(("finish strip at cells %s, grid at %s" % (pr.package.finish[:2], pr.package.grid[:2]), "note"))
        for n in pr.notes:
            lines.append(("note: " + n, "note"))
        if pr.problems:
            lines.append(("%d problem(s) the game would notice:" % len(pr.problems), "bad"))
            for p in pr.problems:
                lines.append((" - " + p, "bad"))
        elif pr.package:
            lines.append(("Validator: clean.  Race the AI on it next.", "good"))
        if extra:
            lines.extend(extra)
        self.say(lines)

    # ---- the picture ----------------------------------------------------------
    def redraw(self, full=False):
        pr = self.project
        if full or self.base_rgb is None:
            if self.view.get() == "tiles" and pr.full is not None:
                rgb = PJ.picture_tiles(pr.theme, pr.full)
            else:
                rgb = PJ.picture_roles(pr.roles, pr.markers)
            if self.show_sectors.get() and pr.package and pr.package.sect:
                rgb = PJ.tint_sectors(rgb, pr.package.sect)
            self.base_rgb = rgb
            base = tk.PhotoImage(data=b"P6 1024 1024 255\n" + rgb)
            if self.zoom >= 2:
                base = base.zoom(int(self.zoom))
            elif self.zoom < 1:
                base = base.subsample(int(round(1 / self.zoom)))
            self.photo = base
            self.canvas.delete("all")
            self.canvas.create_image(0, 0, anchor="nw", image=self.photo, tags="map")
            size = int(1024 * self.zoom)
            self.canvas.configure(scrollregion=(0, 0, size, size))
        self.draw_overlays()

    def z(self, v):
        return v * self.zoom

    def draw_overlays(self):
        c = self.canvas
        c.delete("ov")
        pr = self.project
        s = self.zoom
        # markers in the tiles view (the roles view paints them into the picture)
        if self.view.get() == "tiles":
            for fam, x, y in pr.markers:
                w, h = PJ.FOOTPRINT[fam]
                c.create_rectangle(x * TILE * s, y * TILE * s, (x + w) * TILE * s, (y + h) * TILE * s,
                                   outline=hexrgb(PJ.MARKER_RGB[fam]), width=2, tags="ov")
        if pr.package and self.show_grid.get():
            fx, fy, fw, fh = pr.package.finish
            c.create_rectangle(fx * 16 * s, fy * 16 * s, (fx + fw) * 16 * s, (fy + fh) * 16 * s,
                               outline="white", width=2, tags="ov")
            gx, gy, gs = pr.package.grid
            for slot in range(8):
                x = gx + (gs if slot & 1 else 0)
                y = gy + 24 * slot
                c.create_rectangle((x - 6) * s, (y - 6) * s, (x + 6) * s, (y + 6) * s, outline="yellow", width=2, tags="ov")
        if pr.package and self.show_line.get() and pr.package.line:
            pts = pr.package.line
            flat = []
            for x, y, a in pts + pts[:1]:
                flat += [(x + 4) * s, (y + 4) * s]
            c.create_line(*flat, fill="#ff80ff", width=1, dash=(4, 4), tags="ov")
            for i, (x, y, a) in enumerate(pts):
                r = 4 * s if i != self.selected_wp else 7 * s
                c.create_oval((x + 4) * s - r, (y + 4) * s - r, (x + 4) * s + r, (y + 4) * s + r,
                              fill=ROW_COLOUR[a & 3], outline="white" if i == 0 else "black", width=2 if i == 0 else 1, tags="ov")
        self.status.configure(text=self.status_text())

    def status_text(self, tile=None):
        pr = self.project
        parts = [os.path.relpath(pr.dir) if pr.dir else "(unsaved course)"]
        parts.append("modified" if self.unsaved else "saved")
        parts.append("needs a build" if self.dirty_since_build else "built")
        if tile:
            x, y = tile
            if 0 <= x < 128 and 0 <= y < 128:
                parts.append("tile %d,%d %s" % (x, y, pr.roles[y * 128 + x].lower()))
                if pr.package and pr.package.sect:
                    sec = pr.package.sect[(y // 2) * 64 + x // 2]
                    parts.append("sector %s" % ("-" if sec == 0x7F else sec))
        parts.append("zoom %g" % self.zoom)
        return "   ".join(parts)

    def paint_pixels(self, x0, y0, x1, y1, colour):
        """Update the on-screen picture for a tile rectangle without a full redraw."""
        if self.photo is None:
            return
        s = self.zoom
        self.photo.put(colour, to=(int(x0 * TILE * s), int(y0 * TILE * s), int(x1 * TILE * s), int(y1 * TILE * s)))

    # ---- editing -----------------------------------------------------------------
    def snapshot(self):
        self.undo_stack.append((list(self.project.roles), list(self.project.markers)))
        if len(self.undo_stack) > 60:
            self.undo_stack.pop(0)
        self.redo_stack.clear()

    def undo(self):
        if not self.undo_stack:
            return
        self.redo_stack.append((list(self.project.roles), list(self.project.markers)))
        self.project.roles, self.project.markers = self.undo_stack.pop()
        self.touched()
        self.redraw(full=True)

    def redo(self):
        if not self.redo_stack:
            return
        self.undo_stack.append((list(self.project.roles), list(self.project.markers)))
        self.project.roles, self.project.markers = self.redo_stack.pop()
        self.touched()
        self.redraw(full=True)

    def touched(self):
        self.unsaved = True
        self.dirty_since_build = True

    def tile_at(self, event):
        x = self.canvas.canvasx(event.x) / (TILE * self.zoom)
        y = self.canvas.canvasy(event.y) / (TILE * self.zoom)
        return int(x), int(y)

    def marker_at(self, tx, ty):
        for k, (fam, x, y) in enumerate(self.project.markers):
            w, h = PJ.FOOTPRINT[fam]
            if x <= tx < x + w and y <= ty < y + h:
                return k
        return -1

    def paint_role(self, tx, ty, role):
        b = int(self.brush.get())
        x0, y0 = max(0, tx - b // 2), max(0, ty - b // 2)
        x1, y1 = min(128, x0 + b), min(128, y0 + b)
        pr = self.project
        for y in range(y0, y1):
            for x in range(x0, x1):
                i = y * 128 + x
                if pr.roles[i] == "LINE" and role == "ROAD":
                    continue
                pr.roles[i] = role
        # a marker needs road under it
        pr.markers = [m for m in pr.markers if not self._marker_overlaps(m, x0, y0, x1, y1) or role == "ROAD"]
        self.paint_pixels(x0, y0, x1, y1, hexrgb(PJ.ROLE_RGB[role]))

    def _marker_overlaps(self, m, x0, y0, x1, y1):
        fam, x, y = m
        w, h = PJ.FOOTPRINT[fam]
        return not (x + w <= x0 or x >= x1 or y + h <= y0 or y >= y1)

    def draw_line(self, tx, ty):
        """The start line across the road at the clicked tile."""
        pr = self.project
        if pr.roles[ty * 128 + tx] not in ("ROAD", "LINE"):
            self.status.configure(text="click a road tile: the line runs across the road there")
            return
        for i, r in enumerate(pr.roles):
            if r == "LINE":
                pr.roles[i] = "ROAD"
        x0 = tx
        while x0 > 0 and pr.roles[ty * 128 + x0 - 1] == "ROAD":
            x0 -= 1
        x1 = tx
        while x1 < 127 and pr.roles[ty * 128 + x1 + 1] == "ROAD":
            x1 += 1
        for x in range(x0, x1 + 1):
            pr.roles[ty * 128 + x] = "LINE"
        self.redraw(full=True)

    def place_marker(self, fam, tx, ty):
        pr = self.project
        w, h = PJ.FOOTPRINT[fam]
        if tx + w > 128 or ty + h > 128:
            return
        k = self.marker_at(tx, ty)
        if k >= 0:
            pr.markers.pop(k)
        for y in range(ty, ty + h):
            for x in range(tx, tx + w):
                if pr.roles[y * 128 + x] != "LINE":
                    pr.roles[y * 128 + x] = "ROAD"
        pr.markers.append((fam, tx, ty))
        self.redraw(full=True)

    def on_press(self, event):
        if self.busy:
            return
        tx, ty = self.tile_at(event)
        if not (0 <= tx < 128 and 0 <= ty < 128):
            return
        tool = self.tool.get()
        if tool == "WP":
            self.pick_waypoint(tx, ty)
            return
        self.snapshot()
        self.touched()
        if tool == "LINE":
            self.draw_line(tx, ty)
        elif tool.startswith("M:"):
            self.place_marker(tool[2:], tx, ty)
        else:
            self.paint_role(tx, ty, tool)
        self.status.configure(text=self.status_text((tx, ty)))

    def on_drag(self, event):
        if self.busy:
            return
        tx, ty = self.tile_at(event)
        if not (0 <= tx < 128 and 0 <= ty < 128):
            return
        tool = self.tool.get()
        if tool == "WP":
            self.drag_waypoint(tx, ty, event)
        elif tool in PJ.ROLE_RGB and tool != "LINE":
            self.touched()
            self.paint_role(tx, ty, tool)

    def on_release(self, event):
        tool = self.tool.get()
        if tool == "WP" and self.selected_wp >= 0:
            self.project.save_line()
            self.recheck()
        elif tool in PJ.ROLE_RGB:
            self.redraw(full=True)

    def on_right(self, event):
        if self.busy:
            return
        tx, ty = self.tile_at(event)
        if not (0 <= tx < 128 and 0 <= ty < 128):
            return
        k = self.marker_at(tx, ty)
        if k >= 0:
            self.snapshot(); self.touched()
            self.project.markers.pop(k)
            self.redraw(full=True)
        elif self.tool.get() in PJ.ROLE_RGB and self.tool.get() != "LINE":
            if not self.undo_stack or event.type == tk.EventType.ButtonPress:
                self.snapshot()
            self.touched()
            self.paint_role(tx, ty, "OFF")

    def on_move(self, event):
        self.status.configure(text=self.status_text(self.tile_at(event)))

    def on_wheel(self, event):
        self.canvas.yview_scroll(-1 if event.delta > 0 else 1, "units")

    def set_zoom(self, z):
        z = max(0.5, min(4, z))
        if z != self.zoom:
            self.zoom = z
            self.redraw(full=True)

    # ---- waypoints ------------------------------------------------------------
    def pick_waypoint(self, tx, ty):
        pr = self.project
        if not pr.package:
            self.status.configure(text="build the course first: the waypoints come from the build")
            return
        best, bd = -1, 1e9
        for i, (x, y, a) in enumerate(pr.package.line):
            d = (x // 8 - tx) ** 2 + (y // 8 - ty) ** 2
            if d < bd:
                bd, best = d, i
        self.selected_wp = best if bd <= 9 else -1
        self.draw_overlays()

    def drag_waypoint(self, tx, ty, event):
        pr = self.project
        if self.selected_wp < 0 or not pr.package:
            return
        x, y, a = pr.package.line[self.selected_wp]
        pr.package.line[self.selected_wp] = (tx * 8, ty * 8, a)
        self.unsaved = True
        self.draw_overlays()

    def set_row(self, row):
        pr = self.project
        if self.selected_wp < 0 or not pr.package:
            return
        x, y, a = pr.package.line[self.selected_wp]
        pr.package.line[self.selected_wp] = (x, y, (a & 0x80) | row)
        self.unsaved = True
        pr.save_line()
        self.draw_overlays()
        self.status.configure(text="waypoint %d: AI speed row %d (0 slow .. 3 fast)" % (self.selected_wp, row))

    def recheck(self):
        pr = self.project
        if pr.package:
            pr.problems = PJ.lint_package(pr.package)
            self.show_verdict()

    # ---- files ------------------------------------------------------------------
    def pull_keys(self):
        pr = self.project
        pr.name = self.name_var.get().strip() or pr.name
        pr.theme = PJ.THEME_NAMES.index(self.theme_var.get())
        pr.items = int(self.items_var.get())

    def push_keys(self):
        pr = self.project
        self.name_var.set(pr.name)
        self.theme_var.set(PJ.THEME_NAMES[pr.theme])
        self.items_var.set(pr.items)

    def on_theme(self):
        self.pull_keys()
        self.touched()
        if self.view.get() == "tiles":
            self.view.set("roles")
        self.redraw(full=True)

    def default_dir(self):
        slug = "".join(ch if ch.isalnum() else "-" for ch in self.project.name.lower()).strip("-") or "course"
        return os.path.join(PJ.ROOT, "tracks", slug)

    def new_course(self):
        if not self.confirm_discard():
            return
        dlg = NewDialog(self.root)
        self.root.wait_window(dlg)
        if not dlg.result:
            return
        name, theme, oval = dlg.result
        self.project = PJ.Project.new(name, theme, oval)
        self.push_keys()
        self.undo_stack.clear(); self.redo_stack.clear()
        self.unsaved = True; self.dirty_since_build = True
        self.selected_wp = -1
        self.say([("A new course.  Paint the road, place the start line, press Build.", "note")])
        self.redraw(full=True)

    def reset(self, oval):
        if not self.confirm_discard():
            return
        self.snapshot()
        pr = self.project
        pr.roles, pr.markers = PJ.parse_roles("\n".join(PJ.template_roles() if oval else PJ.blank_roles()))
        self.touched()
        self.redraw(full=True)

    def confirm_discard(self):
        if not self.unsaved:
            return True
        return messagebox.askyesno("Unsaved changes", "Discard the unsaved changes to this course?")

    def open_dialog(self):
        if not self.confirm_discard():
            return
        d = filedialog.askdirectory(title="Open a course package", initialdir=os.path.join(PJ.ROOT, "tracks"))
        if d:
            self.open_dir(d)

    def open_dir(self, d):
        try:
            self.project = PJ.Project.load(d)
        except Exception as e:
            messagebox.showerror("Cannot open", "%s: %s" % (d, e))
            return
        self.push_keys()
        self.undo_stack.clear(); self.redo_stack.clear()
        self.unsaved = False
        self.dirty_since_build = self.project.package is None
        self.selected_wp = -1
        if self.project.package:
            self.project.problems = PJ.lint_package(self.project.package)
            self.view.set("tiles")
        self.show_verdict()
        self.redraw(full=True)

    def save(self):
        self.pull_keys()
        pr = self.project
        if not pr.dir:
            return self.save_as()
        pr.save_roles(pr.dir)
        self._write_keys(pr.dir)
        if pr.package:
            pr.package.name, pr.package.theme, pr.package.items = pr.name, pr.theme, pr.items
            pr.save_line()
        self.unsaved = False
        self.status.configure(text=self.status_text())

    def _write_keys(self, d):
        """course.txt's own keys follow the editor even before a build."""
        pr = self.project
        path = os.path.join(d, "course.txt")
        lines = open(path).read().splitlines() if os.path.exists(path) else ["format   1"]
        out, seen = [], set()
        for ln in lines:
            k = ln.split(None, 1)[0] if ln.strip() and not ln.startswith("#") else None
            if k in ("name", "theme", "items"):
                if k in seen:
                    continue
                seen.add(k)
                ln = {"name": "name     %s" % pr.name, "theme": "theme    %d" % pr.theme, "items": "items    %d" % pr.items}[k]
            out.append(ln)
        for k in ("name", "theme", "items"):
            if k not in seen:
                out.append({"name": "name     %s" % pr.name, "theme": "theme    %d" % pr.theme, "items": "items    %d" % pr.items}[k])
        with open(path, "w") as f:
            f.write("\n".join(out) + "\n")

    def save_as(self):
        self.pull_keys()
        d = filedialog.askdirectory(title="Save the course as a package directory (it is created if missing)",
                                    initialdir=os.path.dirname(self.default_dir()), mustexist=False)
        if not d:
            return
        self.project.dir = d
        self.save()
        if self.project.package:
            self.project.save_line()

    def export_png(self):
        p = filedialog.asksaveasfilename(title="Export the picture", defaultextension=".png",
                                         initialfile=os.path.basename(self.project.dir or "course") + ".png")
        if p and self.base_rgb:
            PJ.png_write(p, 1024, 1024, self.base_rgb)

    def quit(self):
        if self.confirm_discard():
            self.root.destroy()

    # ---- build / race / play -----------------------------------------------------
    def set_busy(self, on, msg=""):
        self.busy = on
        state = "disabled" if on else "normal"
        for b in (self.b_build, self.b_race, self.b_play, self.b_tt):
            b.configure(state=state)
        self.progress.configure(text=msg)

    def build(self):
        if self.busy:
            return
        self.pull_keys()
        pr = self.project
        if not pr.dir:
            pr.dir = self.default_dir()
        d = pr.dir
        self.set_busy(True, "building...")
        def work():
            try:
                ok = pr.build(d, progress=lambda m: self.q.put(("progress", m)))
                self.q.put(("built", ok))
            except Exception as e:
                self.q.put(("error", "build failed: %s" % e))
        threading.Thread(target=work, daemon=True).start()

    def race(self):
        if self.busy:
            return
        pr = self.project
        if not pr.package or self.dirty_since_build:
            self.say([("Build the course first.", "bad")])
            return
        self.set_busy(True, "the ROM's field is driving it at three classes...")
        def work():
            ok, out = PJ.run_ailap(pr.dir)
            self.q.put(("raced", (ok, out)))
        threading.Thread(target=work, daemon=True).start()

    def play(self, timetrial):
        pr = self.project
        if not pr.package or self.dirty_since_build:
            self.say([("Build the course first.", "bad")])
            return
        if pr.problems:
            if not messagebox.askyesno("Problems", "The validator reported %d problem(s).  Play it anyway?" % len(pr.problems)):
                return
        if PJ.launch_game(pr.dir, timetrial) is None:
            messagebox.showerror("No game binary", "build-native/smk is not built: run `make` in the repository first")

    def _poll(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "progress":
                    self.progress.configure(text=val)
                elif kind == "built":
                    self.set_busy(False, "")
                    self.dirty_since_build = not val
                    self.unsaved = False
                    self.selected_wp = -1
                    if val:
                        self.view.set("tiles")
                    self.show_verdict()
                    self.redraw(full=True)
                elif kind == "raced":
                    self.set_busy(False, "")
                    ok, out = val
                    lines = [(("The field laps it at every class." if ok else "The field did NOT lap it."), "good" if ok else "bad")]
                    for ln in out.splitlines():
                        if "cc:" in ln or "runs" in ln or "hazard" in ln:
                            lines.append((ln.strip(), "note" if "0 hazard" in ln else "bad" if "NO LAP" in ln or "off the road" in ln else "note"))
                    self.show_verdict(lines)
                elif kind == "error":
                    self.set_busy(False, "")
                    self.say([(val, "bad")])
        except queue.Empty:
            pass
        self.root.after(100, self._poll)

    def help(self):
        messagebox.showinfo("How it works",
            "Paint the road with the brush (left button; right button paints off-road).\n"
            "Draw walls, water and void the same way.\n"
            "Pick 'Start line' and click the road where the race starts: the karts drive UP from it,\n"
            "so the start straight must run towards the top of the map.\n"
            "Place boxes, coins, oil, pads, ramps and obstacles by clicking; right click removes.\n"
            "Choose a theme: it decides the tiles, the music, the creatures and how the ground behaves.\n\n"
            "Build and validate: the tiles, the sector map and the racing line are generated and checked.\n"
            "Race the AI: the game's own field must lap it at 50, 100 and 150cc without leaving the road.\n"
            "Play: the game starts on it.  The course also appears under CUSTOM on the course screen.\n\n"
            "Tune: with 'Waypoints', drag a waypoint or press 0-3 to set the AI's speed row there.")


class NewDialog(tk.Toplevel):
    def __init__(self, parent):
        super().__init__(parent)
        self.title("New course")
        self.result = None
        self.transient(parent)
        f = ttk.Frame(self, padding=10); f.pack()
        ttk.Label(f, text="Name").grid(row=0, column=0, sticky="w")
        self.name = tk.StringVar(value="MY COURSE")
        ttk.Entry(f, textvariable=self.name, width=24).grid(row=0, column=1, pady=2)
        ttk.Label(f, text="Theme").grid(row=1, column=0, sticky="w")
        self.theme = tk.StringVar(value=PJ.THEME_NAMES[1])
        ttk.Combobox(f, textvariable=self.theme, values=PJ.THEME_NAMES, state="readonly", width=22).grid(row=1, column=1, pady=2)
        self.oval = tk.BooleanVar(value=True)
        ttk.Checkbutton(f, text="Start from the oval (else a blank map)", variable=self.oval).grid(row=2, column=0, columnspan=2, sticky="w", pady=4)
        b = ttk.Frame(f); b.grid(row=3, column=0, columnspan=2, pady=(6, 0))
        ttk.Button(b, text="Create", command=self.ok).pack(side="left", padx=4)
        ttk.Button(b, text="Cancel", command=self.destroy).pack(side="left", padx=4)
        self.bind("<Return>", lambda e: self.ok())
        self.grab_set()

    def ok(self):
        self.result = (self.name.get().strip() or "MY COURSE", PJ.THEME_NAMES.index(self.theme.get()), self.oval.get())
        self.destroy()


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else None
    try:
        PJ.load_rom()
    except FileNotFoundError as e:
        sys.exit(str(e))
    root = tk.Tk()
    root.geometry("1500x1060")
    Studio(root, path)
    root.mainloop()


if __name__ == "__main__":
    main()

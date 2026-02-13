"""
VVD (Versatile Vector Diagram) Python Library
==============================================

Provides:
  - VVDParser:   parse VVD text format into a command list
  - VVDBinary:   serialize commands to binary (.vvb) and deserialize back
  - VVDRenderer: render command list to PIL Image or SVG string
  - VVDWriter:   programmatically build VVD documents (fluent API)
"""

from __future__ import annotations

import math
import os
import re
import struct
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple, Union

# ---------------------------------------------------------------------------
# Command representation
# ---------------------------------------------------------------------------

@dataclass
class Cmd:
    """A single VVD command."""
    op: str                    # PAL, DEF, END, SIZ, ROT, MOV, LIN, ARC, SYM, TXT, INC
    args: Dict[str, Any] = field(default_factory=dict)

    def __repr__(self):
        return f"Cmd({self.op!r}, {self.args})"


# ---------------------------------------------------------------------------
# VVDParser – text format → command list
# ---------------------------------------------------------------------------

class VVDParser:
    """Parse a VVD text file/string into a list of Cmd objects."""

    _TOKEN_RE = re.compile(
        r'"[^"]*"'           # quoted string
        r'|#[0-9A-Fa-f]{6}'  # hex color
        r'|[-+]?\d+\.?\d*'   # number
        r'|[A-Z]+'           # keyword
    )

    def parse_file(self, path: str) -> List[Cmd]:
        with open(path, 'r') as f:
            text = f.read()
        return self.parse(text, base_dir=os.path.dirname(os.path.abspath(path)))

    def parse(self, text: str, base_dir: str = '.') -> List[Cmd]:
        commands: List[Cmd] = []
        lines = text.split('\n')
        i = 0
        while i < len(lines):
            line = lines[i].strip()
            i += 1

            # skip blanks, comments, VVD1 header marker
            if not line or line.startswith('#'):
                continue
            if line == 'VVD1':
                continue

            # strip inline comments
            comment_pos = line.find('#')
            if comment_pos >= 0:
                # make sure it's not inside a string or a hex color
                in_str = False
                for ci, ch in enumerate(line):
                    if ch == '"':
                        in_str = not in_str
                    if ch == '#' and not in_str:
                        # Check if this is a hex color (#RRGGBB)
                        remaining = line[ci:]
                        if not re.match(r'^#[0-9A-Fa-f]{6}\b', remaining):
                            line = line[:ci].strip()
                        break

            tokens = self._TOKEN_RE.findall(line)
            if not tokens:
                continue

            op = tokens[0].upper()
            rest = tokens[1:]

            if op == 'PAL':
                slot = int(rest[0])
                color = rest[1]  # e.g. #3366CC
                commands.append(Cmd('PAL', {'id': slot, 'color': color}))

            elif op == 'DEF':
                def_id = int(rest[0])
                sub_cmds: List[Cmd] = []
                # collect until END
                while i < len(lines):
                    dline = lines[i].strip()
                    i += 1
                    if not dline or dline.startswith('#'):
                        continue
                    dtokens = self._TOKEN_RE.findall(dline)
                    if not dtokens:
                        continue
                    dop = dtokens[0].upper()
                    drest = dtokens[1:]
                    if dop == 'END':
                        break
                    elif dop == 'MOV':
                        sub_cmds.append(Cmd('MOV', {'x': _num(drest[0]), 'y': _num(drest[1])}))
                    elif dop == 'LIN':
                        sub_cmds.append(Cmd('LIN', {'x': _num(drest[0]), 'y': _num(drest[1])}))
                    elif dop == 'ARC':
                        sub_cmds.append(Cmd('ARC', {
                            'cx': _num(drest[0]), 'cy': _num(drest[1]),
                            'r': _num(drest[2]),
                            'start_deg': _num(drest[3]), 'end_deg': _num(drest[4])
                        }))
                commands.append(Cmd('DEF', {'id': def_id, 'commands': sub_cmds}))

            elif op == 'SIZ':
                commands.append(Cmd('SIZ', {'scale': _num(rest[0])}))

            elif op == 'ROT':
                commands.append(Cmd('ROT', {'degrees': _num(rest[0])}))

            elif op == 'MOV':
                commands.append(Cmd('MOV', {'x': _num(rest[0]), 'y': _num(rest[1])}))

            elif op == 'LIN':
                commands.append(Cmd('LIN', {
                    'x': _num(rest[0]), 'y': _num(rest[1]),
                    'col': int(rest[2])
                }))

            elif op == 'ARC':
                commands.append(Cmd('ARC', {
                    'cx': _num(rest[0]), 'cy': _num(rest[1]),
                    'r': _num(rest[2]),
                    'start_deg': _num(rest[3]), 'end_deg': _num(rest[4]),
                    'col': int(rest[5])
                }))

            elif op == 'SYM':
                commands.append(Cmd('SYM', {
                    'id': int(rest[0]),
                    'x': _num(rest[1]), 'y': _num(rest[2]),
                    'col': int(rest[3])
                }))

            elif op == 'TXT':
                # first token in rest should be the quoted string
                s = rest[0].strip('"')
                commands.append(Cmd('TXT', {
                    'text': s,
                    'x': _num(rest[1]), 'y': _num(rest[2]),
                    'size': _num(rest[3]),
                    'col': int(rest[4])
                }))

            elif op == 'INC':
                fname = rest[0].strip('"')
                commands.append(Cmd('INC', {'filename': fname}))

            elif op == 'END':
                commands.append(Cmd('END', {}))

        return commands


def _num(s: str) -> Union[int, float]:
    """Parse a numeric string, returning int if possible, else float."""
    if '.' in s:
        return float(s)
    return int(s)


# ---------------------------------------------------------------------------
# VVDBinary – binary serialization / deserialization
# ---------------------------------------------------------------------------

class VVDBinary:
    """Serialize VVD commands to binary (.vvb) and deserialize back."""

    MAGIC = b'VVD1'
    VERSION = 0x02

    # Top-level opcodes
    OP_PAL = 0x01
    OP_DEF = 0x02
    OP_SIZ = 0x03
    OP_ROT = 0x04
    OP_MOV = 0x05
    OP_LIN = 0x06
    OP_ARC = 0x07
    OP_SYM = 0x08
    OP_TXT = 0x09
    OP_INC = 0x0A
    OP_END = 0xFF

    # DEF sub-command tags
    SUB_MOV = 0x00
    SUB_LIN = 0x01
    SUB_ARC = 0x02

    def serialize(self, commands: List[Cmd], coord_scale: int = 1) -> bytes:
        """Serialize a command list to VVB binary format."""
        buf = bytearray()
        # Header
        buf += self.MAGIC
        buf += struct.pack('<B', self.VERSION)
        buf += struct.pack('<H', coord_scale)

        for cmd in commands:
            buf += self._encode_cmd(cmd, coord_scale)

        return bytes(buf)

    def serialize_to_file(self, commands: List[Cmd], path: str, coord_scale: int = 1):
        data = self.serialize(commands, coord_scale)
        with open(path, 'wb') as f:
            f.write(data)

    def deserialize(self, data: bytes) -> Tuple[List[Cmd], int]:
        """Deserialize VVB binary data. Returns (commands, coord_scale)."""
        if data[:4] != self.MAGIC:
            raise ValueError("Invalid VVB magic number")
        version = data[4]
        if version != self.VERSION:
            raise ValueError(f"Unsupported VVB version: {version}")
        coord_scale = struct.unpack_from('<H', data, 5)[0]
        if coord_scale == 0:
            coord_scale = 1

        commands: List[Cmd] = []
        pos = 7  # after header

        while pos < len(data):
            opcode = data[pos]
            pos += 1

            if opcode == self.OP_END:
                break

            elif opcode == self.OP_PAL:
                slot = data[pos]
                r, g, b = data[pos+1], data[pos+2], data[pos+3]
                pos += 4
                color = f'#{r:02X}{g:02X}{b:02X}'
                commands.append(Cmd('PAL', {'id': slot, 'color': color}))

            elif opcode == self.OP_DEF:
                def_id = struct.unpack_from('<H', data, pos)[0]; pos += 2
                sub_count = struct.unpack_from('<H', data, pos)[0]; pos += 2
                sub_cmds: List[Cmd] = []
                for _ in range(sub_count):
                    tag = data[pos]; pos += 1
                    if tag == self.SUB_MOV:
                        x = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        y = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        sub_cmds.append(Cmd('MOV', {
                            'x': x / coord_scale, 'y': y / coord_scale
                        }))
                    elif tag == self.SUB_LIN:
                        x = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        y = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        sub_cmds.append(Cmd('LIN', {
                            'x': x / coord_scale, 'y': y / coord_scale
                        }))
                    elif tag == self.SUB_ARC:
                        cx = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        cy = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        r = struct.unpack_from('<H', data, pos)[0]; pos += 2
                        sd = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        ed = struct.unpack_from('<h', data, pos)[0]; pos += 2
                        sub_cmds.append(Cmd('ARC', {
                            'cx': cx / coord_scale, 'cy': cy / coord_scale,
                            'r': r / coord_scale,
                            'start_deg': sd / 10.0, 'end_deg': ed / 10.0
                        }))
                commands.append(Cmd('DEF', {'id': def_id, 'commands': sub_cmds}))

            elif opcode == self.OP_SIZ:
                val = struct.unpack_from('<H', data, pos)[0]; pos += 2
                commands.append(Cmd('SIZ', {'scale': val / 100.0}))

            elif opcode == self.OP_ROT:
                val = struct.unpack_from('<h', data, pos)[0]; pos += 2
                commands.append(Cmd('ROT', {'degrees': val / 10.0}))

            elif opcode == self.OP_MOV:
                x = struct.unpack_from('<h', data, pos)[0]; pos += 2
                y = struct.unpack_from('<h', data, pos)[0]; pos += 2
                commands.append(Cmd('MOV', {
                    'x': x / coord_scale, 'y': y / coord_scale
                }))

            elif opcode == self.OP_LIN:
                x = struct.unpack_from('<h', data, pos)[0]; pos += 2
                y = struct.unpack_from('<h', data, pos)[0]; pos += 2
                col = data[pos]; pos += 1
                commands.append(Cmd('LIN', {
                    'x': x / coord_scale, 'y': y / coord_scale,
                    'col': col
                }))

            elif opcode == self.OP_ARC:
                cx = struct.unpack_from('<h', data, pos)[0]; pos += 2
                cy = struct.unpack_from('<h', data, pos)[0]; pos += 2
                r = struct.unpack_from('<H', data, pos)[0]; pos += 2
                sd = struct.unpack_from('<h', data, pos)[0]; pos += 2
                ed = struct.unpack_from('<h', data, pos)[0]; pos += 2
                col = data[pos]; pos += 1
                commands.append(Cmd('ARC', {
                    'cx': cx / coord_scale, 'cy': cy / coord_scale,
                    'r': r / coord_scale,
                    'start_deg': sd / 10.0, 'end_deg': ed / 10.0,
                    'col': col
                }))

            elif opcode == self.OP_SYM:
                sid = struct.unpack_from('<H', data, pos)[0]; pos += 2
                x = struct.unpack_from('<h', data, pos)[0]; pos += 2
                y = struct.unpack_from('<h', data, pos)[0]; pos += 2
                col = data[pos]; pos += 1
                commands.append(Cmd('SYM', {
                    'id': sid,
                    'x': x / coord_scale, 'y': y / coord_scale,
                    'col': col
                }))

            elif opcode == self.OP_TXT:
                slen = data[pos]; pos += 1
                s = data[pos:pos+slen].decode('utf-8'); pos += slen
                x = struct.unpack_from('<h', data, pos)[0]; pos += 2
                y = struct.unpack_from('<h', data, pos)[0]; pos += 2
                size = struct.unpack_from('<H', data, pos)[0]; pos += 2
                col = data[pos]; pos += 1
                commands.append(Cmd('TXT', {
                    'text': s,
                    'x': x / coord_scale, 'y': y / coord_scale,
                    'size': size / 10.0,
                    'col': col
                }))

            elif opcode == self.OP_INC:
                slen = data[pos]; pos += 1
                s = data[pos:pos+slen].decode('utf-8'); pos += slen
                commands.append(Cmd('INC', {'filename': s}))

            else:
                raise ValueError(f"Unknown opcode 0x{opcode:02X} at position {pos-1}")

        return commands, coord_scale

    def deserialize_file(self, path: str) -> Tuple[List[Cmd], int]:
        with open(path, 'rb') as f:
            data = f.read()
        return self.deserialize(data)

    # -- internal encoding helpers --

    def _encode_cmd(self, cmd: Cmd, cs: int) -> bytes:
        buf = bytearray()
        a = cmd.args

        if cmd.op == 'PAL':
            buf += struct.pack('<B', self.OP_PAL)
            buf += struct.pack('<B', a['id'])
            color = a['color'].lstrip('#')
            r, g, b = int(color[0:2], 16), int(color[2:4], 16), int(color[4:6], 16)
            buf += struct.pack('<BBB', r, g, b)

        elif cmd.op == 'DEF':
            buf += struct.pack('<B', self.OP_DEF)
            sub_cmds = a['commands']
            buf += struct.pack('<H', a['id'])
            buf += struct.pack('<H', len(sub_cmds))
            for sc in sub_cmds:
                buf += self._encode_sub_cmd(sc, cs)

        elif cmd.op == 'SIZ':
            buf += struct.pack('<B', self.OP_SIZ)
            buf += struct.pack('<H', int(round(a['scale'] * 100)))

        elif cmd.op == 'ROT':
            buf += struct.pack('<B', self.OP_ROT)
            buf += struct.pack('<h', int(round(a['degrees'] * 10)))

        elif cmd.op == 'MOV':
            buf += struct.pack('<B', self.OP_MOV)
            buf += struct.pack('<hh', _sc(a['x'], cs), _sc(a['y'], cs))

        elif cmd.op == 'LIN':
            buf += struct.pack('<B', self.OP_LIN)
            buf += struct.pack('<hh', _sc(a['x'], cs), _sc(a['y'], cs))
            buf += struct.pack('<B', a['col'])

        elif cmd.op == 'ARC':
            buf += struct.pack('<B', self.OP_ARC)
            buf += struct.pack('<hh', _sc(a['cx'], cs), _sc(a['cy'], cs))
            buf += struct.pack('<H', _sc_u(a['r'], cs))
            buf += struct.pack('<hh',
                int(round(a['start_deg'] * 10)),
                int(round(a['end_deg'] * 10)))
            buf += struct.pack('<B', a['col'])

        elif cmd.op == 'SYM':
            buf += struct.pack('<B', self.OP_SYM)
            buf += struct.pack('<H', a['id'])
            buf += struct.pack('<hh', _sc(a['x'], cs), _sc(a['y'], cs))
            buf += struct.pack('<B', a['col'])

        elif cmd.op == 'TXT':
            buf += struct.pack('<B', self.OP_TXT)
            s = a['text'].encode('utf-8')
            buf += struct.pack('<B', len(s))
            buf += s
            buf += struct.pack('<hh', _sc(a['x'], cs), _sc(a['y'], cs))
            buf += struct.pack('<H', int(round(a['size'] * 10)))
            buf += struct.pack('<B', a['col'])

        elif cmd.op == 'INC':
            buf += struct.pack('<B', self.OP_INC)
            s = a['filename'].encode('utf-8')
            buf += struct.pack('<B', len(s))
            buf += s

        elif cmd.op == 'END':
            buf += struct.pack('<B', self.OP_END)

        return bytes(buf)

    def _encode_sub_cmd(self, cmd: Cmd, cs: int) -> bytes:
        buf = bytearray()
        a = cmd.args
        if cmd.op == 'MOV':
            buf += struct.pack('<B', self.SUB_MOV)
            buf += struct.pack('<hh', _sc(a['x'], cs), _sc(a['y'], cs))
        elif cmd.op == 'LIN':
            buf += struct.pack('<B', self.SUB_LIN)
            buf += struct.pack('<hh', _sc(a['x'], cs), _sc(a['y'], cs))
        elif cmd.op == 'ARC':
            buf += struct.pack('<B', self.SUB_ARC)
            buf += struct.pack('<hh', _sc(a['cx'], cs), _sc(a['cy'], cs))
            buf += struct.pack('<H', _sc_u(a['r'], cs))
            buf += struct.pack('<hh',
                int(round(a['start_deg'] * 10)),
                int(round(a['end_deg'] * 10)))
        return bytes(buf)


def _sc(val, cs: int) -> int:
    """Scale a coordinate value for binary encoding (signed)."""
    return int(round(val * cs))

def _sc_u(val, cs: int) -> int:
    """Scale a coordinate value for binary encoding (unsigned)."""
    return int(round(abs(val) * cs))


# ---------------------------------------------------------------------------
# VVDRenderer – render to PIL Image or SVG string
# ---------------------------------------------------------------------------

class VVDRenderer:
    """Render a VVD command list to a PIL Image or SVG string."""

    def __init__(self):
        self.palette: Dict[int, str] = {}  # slot -> '#RRGGBB'
        self.defs: Dict[int, List[Cmd]] = {}
        self.pen_x: float = 0.0
        self.pen_y: float = 0.0
        self.scale: float = 1.0
        self.rotation: float = 0.0  # degrees

    def _reset_state(self):
        self.palette = {}
        self.defs = {}
        self.pen_x = 0.0
        self.pen_y = 0.0
        self.scale = 1.0
        self.rotation = 0.0

    def _get_color(self, col: int) -> str:
        return self.palette.get(col, '#000000')

    def _compute_bounds(self, commands: List[Cmd], margin: float = 10.0):
        """Compute bounding box of all drawing commands."""
        xs, ys = [0.0], [0.0]
        pen_x, pen_y = 0.0, 0.0
        scale = 1.0
        rotation = 0.0

        for cmd in commands:
            a = cmd.args
            if cmd.op == 'PAL':
                continue
            elif cmd.op == 'DEF':
                continue
            elif cmd.op == 'SIZ':
                scale = a['scale']
            elif cmd.op == 'ROT':
                rotation = a['degrees']
            elif cmd.op == 'MOV':
                pen_x, pen_y = a['x'], a['y']
                xs.append(pen_x); ys.append(pen_y)
            elif cmd.op == 'LIN':
                pen_x, pen_y = a['x'], a['y']
                xs.append(pen_x); ys.append(pen_y)
            elif cmd.op == 'ARC':
                cx, cy, r = a['cx'], a['cy'], a['r']
                xs.extend([cx - r, cx + r])
                ys.extend([cy - r, cy + r])
                end_rad = math.radians(a['end_deg'])
                pen_x = cx + r * math.cos(end_rad)
                pen_y = cy + r * math.sin(end_rad)
            elif cmd.op == 'SYM':
                sx, sy = a['x'], a['y']
                sid = a['id']
                if sid in self.defs:
                    for sc in self.defs[sid]:
                        sa = sc.args
                        if sc.op in ('MOV', 'LIN'):
                            rx, ry = self._transform_point(sa['x'], sa.get('y', 0), scale, rotation)
                            xs.append(sx + rx); ys.append(sy + ry)
                        elif sc.op == 'ARC':
                            rx, ry = self._transform_point(sa['cx'], sa['cy'], scale, rotation)
                            tr = sa['r'] * scale
                            xs.extend([sx + rx - tr, sx + rx + tr])
                            ys.extend([sy + ry - tr, sy + ry + tr])
                rotation = 0.0
            elif cmd.op == 'TXT':
                tx, ty = a['x'], a['y']
                sz = a['size']
                tlen = len(a['text'])
                half_w = tlen * sz * 0.35
                xs.extend([tx - half_w, tx + half_w])
                ys.extend([ty - sz, ty + sz])

        min_x = min(xs) - margin
        min_y = min(ys) - margin
        max_x = max(xs) + margin
        max_y = max(ys) + margin
        return min_x, min_y, max_x, max_y

    @staticmethod
    def _transform_point(x: float, y: float, scale: float, rot_deg: float) -> Tuple[float, float]:
        x *= scale
        y *= scale
        if rot_deg != 0:
            rad = math.radians(rot_deg)
            cos_r, sin_r = math.cos(rad), math.sin(rad)
            x, y = x * cos_r - y * sin_r, x * sin_r + y * cos_r
        return x, y

    # ---- SVG Rendering ----

    def render_svg(self, commands: List[Cmd], width: Optional[int] = None,
                   height: Optional[int] = None) -> str:
        """Render commands to an SVG string."""
        self._reset_state()

        # First pass: collect palette and defs
        for cmd in commands:
            if cmd.op == 'PAL':
                self.palette[cmd.args['id']] = cmd.args['color']
            elif cmd.op == 'DEF':
                self.defs[cmd.args['id']] = cmd.args['commands']

        min_x, min_y, max_x, max_y = self._compute_bounds(commands)
        vw = max_x - min_x
        vh = max_y - min_y

        if width is None:
            width = max(int(vw), 100)
        if height is None:
            height = max(int(vh), 100)

        svg_parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'viewBox="{min_x} {min_y} {vw} {vh}" '
            f'width="{width}" height="{height}" '
            f'style="background:white">'
        ]

        self.pen_x = 0.0
        self.pen_y = 0.0
        self.scale = 1.0
        self.rotation = 0.0

        for cmd in commands:
            a = cmd.args
            if cmd.op in ('PAL', 'DEF'):
                continue
            elif cmd.op == 'SIZ':
                self.scale = a['scale']
            elif cmd.op == 'ROT':
                self.rotation = a['degrees']
            elif cmd.op == 'MOV':
                self.pen_x, self.pen_y = a['x'], a['y']
            elif cmd.op == 'LIN':
                color = self._get_color(a['col'])
                svg_parts.append(
                    f'<line x1="{self.pen_x}" y1="{self.pen_y}" '
                    f'x2="{a["x"]}" y2="{a["y"]}" '
                    f'stroke="{color}" stroke-width="1" stroke-linecap="round"/>'
                )
                self.pen_x, self.pen_y = a['x'], a['y']
            elif cmd.op == 'ARC':
                svg_parts.append(self._svg_arc(a))
            elif cmd.op == 'SYM':
                svg_parts.extend(self._svg_sym(a))
            elif cmd.op == 'TXT':
                color = self._get_color(a['col'])
                svg_parts.append(
                    f'<text x="{a["x"]}" y="{a["y"]}" '
                    f'font-size="{a["size"]}" fill="{color}" '
                    f'text-anchor="middle" dominant-baseline="central">'
                    f'{_svg_escape(a["text"])}</text>'
                )

        svg_parts.append('</svg>')
        return '\n'.join(svg_parts)

    def _svg_arc(self, a: Dict) -> str:
        cx, cy, r = a['cx'], a['cy'], a['r']
        start_deg, end_deg = a['start_deg'], a['end_deg']
        color = self._get_color(a['col'])

        sweep = end_deg - start_deg
        if abs(sweep) >= 360:
            # full circle
            self.pen_x = cx + r * math.cos(math.radians(end_deg))
            self.pen_y = cy + r * math.sin(math.radians(end_deg))
            return (
                f'<circle cx="{cx}" cy="{cy}" r="{r}" '
                f'fill="none" stroke="{color}" stroke-width="1"/>'
            )

        start_rad = math.radians(start_deg)
        end_rad = math.radians(end_deg)
        x1 = cx + r * math.cos(start_rad)
        y1 = cy + r * math.sin(start_rad)
        x2 = cx + r * math.cos(end_rad)
        y2 = cy + r * math.sin(end_rad)

        large_arc = 1 if abs(sweep) > 180 else 0
        sweep_flag = 1 if sweep > 0 else 0

        self.pen_x, self.pen_y = x2, y2
        return (
            f'<path d="M {x1} {y1} A {r} {r} 0 {large_arc} {sweep_flag} {x2} {y2}" '
            f'fill="none" stroke="{color}" stroke-width="1" stroke-linecap="round"/>'
        )

    def _svg_sym(self, a: Dict) -> List[str]:
        parts = []
        sid = a['id']
        sx, sy = a['x'], a['y']
        color = self._get_color(a['col'])
        scale = self.scale
        rot = self.rotation
        self.rotation = 0.0  # consumed

        if sid not in self.defs:
            return parts

        local_pen_x, local_pen_y = 0.0, 0.0

        for sc in self.defs[sid]:
            sa = sc.args
            if sc.op == 'MOV':
                local_pen_x, local_pen_y = sa['x'], sa['y']
            elif sc.op == 'LIN':
                # transform both points
                px, py = self._transform_point(local_pen_x, local_pen_y, scale, rot)
                nx, ny = self._transform_point(sa['x'], sa['y'], scale, rot)
                parts.append(
                    f'<line x1="{sx+px}" y1="{sy+py}" '
                    f'x2="{sx+nx}" y2="{sy+ny}" '
                    f'stroke="{color}" stroke-width="1" stroke-linecap="round"/>'
                )
                local_pen_x, local_pen_y = sa['x'], sa['y']
            elif sc.op == 'ARC':
                tcx, tcy = self._transform_point(sa['cx'], sa['cy'], scale, rot)
                tr = sa['r'] * scale
                sd = sa['start_deg'] + rot
                ed = sa['end_deg'] + rot
                sweep = ed - sd
                acx, acy = sx + tcx, sy + tcy

                if abs(sweep) >= 360:
                    parts.append(
                        f'<circle cx="{acx}" cy="{acy}" r="{tr}" '
                        f'fill="none" stroke="{color}" stroke-width="1"/>'
                    )
                else:
                    sr = math.radians(sd)
                    er = math.radians(ed)
                    x1 = acx + tr * math.cos(sr)
                    y1 = acy + tr * math.sin(sr)
                    x2 = acx + tr * math.cos(er)
                    y2 = acy + tr * math.sin(er)
                    la = 1 if abs(sweep) > 180 else 0
                    sf = 1 if sweep > 0 else 0
                    parts.append(
                        f'<path d="M {x1} {y1} A {tr} {tr} 0 {la} {sf} {x2} {y2}" '
                        f'fill="none" stroke="{color}" stroke-width="1" stroke-linecap="round"/>'
                    )
                end_rad = math.radians(ed)
                local_pen_x = sa['cx'] + sa['r'] * math.cos(math.radians(sa['end_deg']))
                local_pen_y = sa['cy'] + sa['r'] * math.sin(math.radians(sa['end_deg']))

        return parts

    # ---- PIL Rendering ----

    def render_pil(self, commands: List[Cmd], width: int = 800, height: int = 600,
                   bg_color: str = '#FFFFFF', line_width: int = 2):
        """Render commands to a PIL Image. Requires Pillow."""
        try:
            from PIL import Image, ImageDraw, ImageFont
        except ImportError:
            raise ImportError("Pillow is required for PIL rendering: pip install Pillow")

        self._reset_state()

        # First pass: collect palette and defs
        for cmd in commands:
            if cmd.op == 'PAL':
                self.palette[cmd.args['id']] = cmd.args['color']
            elif cmd.op == 'DEF':
                self.defs[cmd.args['id']] = cmd.args['commands']

        min_x, min_y, max_x, max_y = self._compute_bounds(commands)
        vw = max_x - min_x
        vh = max_y - min_y

        # compute scale to fit
        sx = width / vw if vw > 0 else 1
        sy = height / vh if vh > 0 else 1
        s = min(sx, sy)
        ox = (width - vw * s) / 2 - min_x * s
        oy = (height - vh * s) / 2 - min_y * s

        def tx(x): return x * s + ox
        def ty(y): return y * s + oy

        img = Image.new('RGB', (width, height), bg_color)
        draw = ImageDraw.Draw(img)

        self.pen_x = 0.0
        self.pen_y = 0.0
        self.scale = 1.0
        self.rotation = 0.0

        for cmd in commands:
            a = cmd.args
            if cmd.op in ('PAL', 'DEF'):
                continue
            elif cmd.op == 'SIZ':
                self.scale = a['scale']
            elif cmd.op == 'ROT':
                self.rotation = a['degrees']
            elif cmd.op == 'MOV':
                self.pen_x, self.pen_y = a['x'], a['y']
            elif cmd.op == 'LIN':
                color = self._get_color(a['col'])
                draw.line(
                    [(tx(self.pen_x), ty(self.pen_y)),
                     (tx(a['x']), ty(a['y']))],
                    fill=color, width=line_width
                )
                self.pen_x, self.pen_y = a['x'], a['y']
            elif cmd.op == 'ARC':
                self._pil_arc(draw, a, tx, ty, s, line_width)
            elif cmd.op == 'SYM':
                self._pil_sym(draw, a, tx, ty, s, line_width)
            elif cmd.op == 'TXT':
                color = self._get_color(a['col'])
                font_size = max(int(a['size'] * s), 8)
                try:
                    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", font_size)
                except (IOError, OSError):
                    try:
                        font = ImageFont.load_default(size=font_size)
                    except TypeError:
                        font = ImageFont.load_default()
                bbox = draw.textbbox((0, 0), a['text'], font=font)
                tw = bbox[2] - bbox[0]
                th = bbox[3] - bbox[1]
                draw.text(
                    (tx(a['x']) - tw / 2, ty(a['y']) - th / 2),
                    a['text'], fill=color, font=font
                )

        return img

    def _pil_arc(self, draw, a, tx, ty, s, lw):
        cx, cy, r = a['cx'], a['cy'], a['r']
        color = self._get_color(a['col'])
        start_deg, end_deg = a['start_deg'], a['end_deg']

        # PIL arc uses bounding box and angles
        bbox = [
            tx(cx - r), ty(cy - r),
            tx(cx + r), ty(cy + r)
        ]

        if abs(end_deg - start_deg) >= 360:
            draw.ellipse(bbox, outline=color, width=lw)
        else:
            draw.arc(bbox, start_deg, end_deg, fill=color, width=lw)

        end_rad = math.radians(end_deg)
        self.pen_x = cx + r * math.cos(end_rad)
        self.pen_y = cy + r * math.sin(end_rad)

    def _pil_sym(self, draw, a, tx, ty, s, lw):
        sid = a['id']
        sx, sy = a['x'], a['y']
        color = self._get_color(a['col'])
        scale = self.scale
        rot = self.rotation
        self.rotation = 0.0

        if sid not in self.defs:
            return

        local_pen_x, local_pen_y = 0.0, 0.0

        for sc in self.defs[sid]:
            sa = sc.args
            if sc.op == 'MOV':
                local_pen_x, local_pen_y = sa['x'], sa['y']
            elif sc.op == 'LIN':
                px, py = self._transform_point(local_pen_x, local_pen_y, scale, rot)
                nx, ny = self._transform_point(sa['x'], sa['y'], scale, rot)
                draw.line(
                    [(tx(sx + px), ty(sy + py)),
                     (tx(sx + nx), ty(sy + ny))],
                    fill=color, width=lw
                )
                local_pen_x, local_pen_y = sa['x'], sa['y']
            elif sc.op == 'ARC':
                tcx, tcy = self._transform_point(sa['cx'], sa['cy'], scale, rot)
                tr = sa['r'] * scale
                sd = sa['start_deg'] + rot
                ed = sa['end_deg'] + rot
                acx, acy = sx + tcx, sy + tcy
                bbox = [
                    tx(acx - tr), ty(acy - tr),
                    tx(acx + tr), ty(acy + tr)
                ]
                if abs(ed - sd) >= 360:
                    draw.ellipse(bbox, outline=color, width=lw)
                else:
                    draw.arc(bbox, sd, ed, fill=color, width=lw)
                local_pen_x = sa['cx'] + sa['r'] * math.cos(math.radians(sa['end_deg']))
                local_pen_y = sa['cy'] + sa['r'] * math.sin(math.radians(sa['end_deg']))


def _svg_escape(text: str) -> str:
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


# ---------------------------------------------------------------------------
# VVDWriter – fluent API for building VVD documents
# ---------------------------------------------------------------------------

class VVDWriter:
    """Programmatically build VVD documents with a fluent API."""

    def __init__(self):
        self._commands: List[Cmd] = []
        self._current_def: Optional[List[Cmd]] = None
        self._current_def_id: Optional[int] = None

    @property
    def commands(self) -> List[Cmd]:
        return list(self._commands)

    def pal(self, slot: int, color: str) -> 'VVDWriter':
        """Set palette color. color should be '#RRGGBB'."""
        self._commands.append(Cmd('PAL', {'id': slot, 'color': color}))
        return self

    def begin_def(self, def_id: int) -> 'VVDWriter':
        """Start a DEF block."""
        if self._current_def is not None:
            raise ValueError("Already inside a DEF block")
        self._current_def = []
        self._current_def_id = def_id
        return self

    def end_def(self) -> 'VVDWriter':
        """End the current DEF block."""
        if self._current_def is None:
            raise ValueError("Not inside a DEF block")
        self._commands.append(Cmd('DEF', {
            'id': self._current_def_id,
            'commands': self._current_def
        }))
        self._current_def = None
        self._current_def_id = None
        return self

    def siz(self, scale: float) -> 'VVDWriter':
        self._commands.append(Cmd('SIZ', {'scale': scale}))
        return self

    def rot(self, degrees: float) -> 'VVDWriter':
        self._commands.append(Cmd('ROT', {'degrees': degrees}))
        return self

    def mov(self, x: float, y: float) -> 'VVDWriter':
        cmd = Cmd('MOV', {'x': x, 'y': y})
        if self._current_def is not None:
            self._current_def.append(cmd)
        else:
            self._commands.append(cmd)
        return self

    def lin(self, x: float, y: float, col: Optional[int] = None) -> 'VVDWriter':
        if self._current_def is not None:
            self._current_def.append(Cmd('LIN', {'x': x, 'y': y}))
        else:
            if col is None:
                raise ValueError("Color required for LIN outside DEF")
            self._commands.append(Cmd('LIN', {'x': x, 'y': y, 'col': col}))
        return self

    def arc(self, cx: float, cy: float, r: float, start_deg: float, end_deg: float,
            col: Optional[int] = None) -> 'VVDWriter':
        if self._current_def is not None:
            self._current_def.append(Cmd('ARC', {
                'cx': cx, 'cy': cy, 'r': r,
                'start_deg': start_deg, 'end_deg': end_deg
            }))
        else:
            if col is None:
                raise ValueError("Color required for ARC outside DEF")
            self._commands.append(Cmd('ARC', {
                'cx': cx, 'cy': cy, 'r': r,
                'start_deg': start_deg, 'end_deg': end_deg,
                'col': col
            }))
        return self

    def sym(self, def_id: int, x: float, y: float, col: int) -> 'VVDWriter':
        self._commands.append(Cmd('SYM', {'id': def_id, 'x': x, 'y': y, 'col': col}))
        return self

    def txt(self, text: str, x: float, y: float, size: float, col: int) -> 'VVDWriter':
        self._commands.append(Cmd('TXT', {
            'text': text, 'x': x, 'y': y, 'size': size, 'col': col
        }))
        return self

    def inc(self, filename: str) -> 'VVDWriter':
        self._commands.append(Cmd('INC', {'filename': filename}))
        return self

    def to_text(self) -> str:
        """Generate VVD text format string."""
        lines = ['VVD1']
        for cmd in self._commands:
            lines.extend(self._cmd_to_text(cmd))
        return '\n'.join(lines) + '\n'

    def save_text(self, path: str):
        with open(path, 'w') as f:
            f.write(self.to_text())

    def save_binary(self, path: str, coord_scale: int = 1):
        binary = VVDBinary()
        binary.serialize_to_file(self._commands, path, coord_scale)

    @staticmethod
    def _cmd_to_text(cmd: Cmd, indent: str = '') -> List[str]:
        a = cmd.args
        if cmd.op == 'PAL':
            return [f'{indent}PAL {a["id"]}, {a["color"]}']
        elif cmd.op == 'DEF':
            lines = [f'{indent}DEF {a["id"]}']
            for sc in a['commands']:
                lines.extend(VVDWriter._cmd_to_text(sc, indent + '  '))
            lines.append(f'{indent}END')
            return lines
        elif cmd.op == 'SIZ':
            return [f'{indent}SIZ {a["scale"]}']
        elif cmd.op == 'ROT':
            return [f'{indent}ROT {a["degrees"]}']
        elif cmd.op == 'MOV':
            return [f'{indent}MOV {_fmt(a["x"])}, {_fmt(a["y"])}']
        elif cmd.op == 'LIN':
            if 'col' in a:
                return [f'{indent}LIN {_fmt(a["x"])}, {_fmt(a["y"])}, {a["col"]}']
            else:
                return [f'{indent}LIN {_fmt(a["x"])}, {_fmt(a["y"])}']
        elif cmd.op == 'ARC':
            if 'col' in a:
                return [f'{indent}ARC {_fmt(a["cx"])}, {_fmt(a["cy"])}, {_fmt(a["r"])}, {_fmt(a["start_deg"])}, {_fmt(a["end_deg"])}, {a["col"]}']
            else:
                return [f'{indent}ARC {_fmt(a["cx"])}, {_fmt(a["cy"])}, {_fmt(a["r"])}, {_fmt(a["start_deg"])}, {_fmt(a["end_deg"])}']
        elif cmd.op == 'SYM':
            return [f'{indent}SYM {a["id"]}, {_fmt(a["x"])}, {_fmt(a["y"])}, {a["col"]}']
        elif cmd.op == 'TXT':
            return [f'{indent}TXT "{a["text"]}", {_fmt(a["x"])}, {_fmt(a["y"])}, {_fmt(a["size"])}, {a["col"]}']
        elif cmd.op == 'INC':
            return [f'{indent}INC "{a["filename"]}"']
        return []


def _fmt(v) -> str:
    """Format a number: drop .0 for integers."""
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return str(v)

// VVD (Versatile Vector Diagram) Library v1.2
// Provides VVDParser, VVDRenderer, and VVDBinary classes.
// Works as both a browser script (globals) and ES module.

(function (root, factory) {
  if (typeof exports === 'object' && typeof module === 'object') {
    module.exports = factory();
  } else if (typeof define === 'function' && define.amd) {
    define([], factory);
  } else {
    var lib = factory();
    root.VVDParser = lib.VVDParser;
    root.VVDRenderer = lib.VVDRenderer;
    root.VVDBinary = lib.VVDBinary;
  }
})(typeof self !== 'undefined' ? self : this, function () {

  // ─── Tokenizer ───────────────────────────────────────────────────────
  function tokenize(line) {
    const tokens = [];
    let i = 0;
    while (i < line.length) {
      while (i < line.length && (line[i] === ' ' || line[i] === ',' || line[i] === '\t')) i++;
      if (i >= line.length) break;
      if (line[i] === '"') {
        i++;
        let s = '';
        while (i < line.length && line[i] !== '"') { s += line[i]; i++; }
        tokens.push(s);
        i++;
      } else {
        let s = '';
        while (i < line.length && line[i] !== ' ' && line[i] !== ',' && line[i] !== '\t') { s += line[i]; i++; }
        tokens.push(s);
      }
    }
    return tokens;
  }

  // ─── VVDParser ───────────────────────────────────────────────────────
  // Parses VVD text source into a structured representation.
  //
  // Returns: {
  //   palette: { [id]: '#rrggbb' },
  //   defs: { [id]: [ { cmd, args } ] },
  //   commands: [ { cmd, args } ]   // scene commands (everything outside DEF blocks)
  // }

  class VVDParser {
    /**
     * Parse VVD text source.
     * @param {string} source - VVD text
     * @returns {{ palette: Object, defs: Object, commands: Array }}
     */
    parse(source) {
      const palette = {};
      const defs = {};
      const commands = [];

      let inDef = false;
      let defId = null;
      let defCmds = [];

      const lines = source.split('\n');

      for (let raw of lines) {
        // Strip comments but preserve hex colors
        let line = raw.replace(/\s#(?![0-9a-fA-F]{3,8}\b).*$/, '')
                      .replace(/^#(?![0-9a-fA-F]{3,8}\b).*$/, '')
                      .trim();
        if (!line || line === 'VVD1') continue;

        const parts = tokenize(line);
        if (!parts.length) continue;
        const cmd = parts[0].toUpperCase();

        if (inDef) {
          if (cmd === 'END') {
            defs[defId] = defCmds;
            inDef = false;
            defCmds = [];
            continue;
          }
          defCmds.push({ cmd, args: parts.slice(1) });
          continue;
        }

        if (cmd === 'PAL') {
          const id = parseInt(parts[1]);
          const hex = parts[2];
          palette[id] = hex;
        } else if (cmd === 'DEF') {
          defId = parseInt(parts[1]);
          inDef = true;
          defCmds = [];
        } else {
          commands.push({ cmd, args: parts.slice(1) });
        }
      }

      return { palette, defs, commands };
    }
  }

  /**
   * Convert parsed VVD data back to VVD text source.
   * @param {{ palette: Object, defs: Object, commands: Array }} parsed
   * @returns {string}
   */
  VVDParser.toSource = function(parsed) {
    const lines = ['VVD1'];

    // Palette
    for (const [id, hex] of Object.entries(parsed.palette)) {
      lines.push('PAL ' + id + ', ' + hex);
    }

    if (Object.keys(parsed.palette).length > 0) lines.push('');

    // Definitions
    for (const [id, cmds] of Object.entries(parsed.defs)) {
      lines.push('DEF ' + id);
      for (const dc of cmds) {
        lines.push('  ' + dc.cmd + ' ' + dc.args.join(', '));
      }
      lines.push('END');
      lines.push('');
    }

    // Scene commands
    for (const sc of parsed.commands) {
      if (sc.cmd === 'TXT') {
        var str = sc.args[0];
        var rest = sc.args.slice(1);
        lines.push('TXT "' + str + '", ' + rest.join(', '));
      } else if (sc.cmd === 'INC') {
        lines.push('INC "' + sc.args[0] + '"');
      } else {
        lines.push(sc.cmd + ' ' + sc.args.join(', '));
      }
    }

    return lines.join('\n');
  };

  // ─── VVDRenderer ─────────────────────────────────────────────────────
  // Renders parsed VVD data onto a canvas.

  class VVDRenderer {
    /**
     * @param {HTMLCanvasElement} canvas
     */
    constructor(canvas) {
      this.canvas = canvas;
      this.ctx = canvas.getContext('2d');
    }

    /**
     * Render VVD source text onto the canvas.
     * @param {string} source - VVD text source
     * @returns {{ defs: number, commands: number }}
     */
    render(source) {
      const parser = new VVDParser();
      const parsed = parser.parse(source);
      return this.renderParsed(parsed);
    }

    /**
     * Render pre-parsed VVD data onto the canvas.
     * @param {{ palette: Object, defs: Object, commands: Array }} parsed
     * @returns {{ defs: number, commands: number }}
     */
    renderParsed(parsed) {
      const ctx = this.ctx;
      const canvas = this.canvas;

      // Resize canvas to container
      const rect = canvas.parentElement.getBoundingClientRect();
      canvas.width = rect.width;
      canvas.height = rect.height;

      // Build full palette array
      const palette = new Array(256).fill('#000000');
      for (const [id, hex] of Object.entries(parsed.palette)) {
        palette[parseInt(id)] = hex;
      }

      const defs = parsed.defs;
      const sceneCmds = parsed.commands;

      // ── Bounding box pass ──
      let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
      const trackPoint = (x, y) => {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      };

      let tmpPenX = 0, tmpPenY = 0, tmpSiz = 1.0, tmpRot = 0;
      for (const { cmd, args } of sceneCmds) {
        switch (cmd) {
          case 'MOV':
            tmpPenX = parseFloat(args[0]); tmpPenY = parseFloat(args[1]);
            trackPoint(tmpPenX, tmpPenY);
            break;
          case 'LIN':
            tmpPenX = parseFloat(args[0]); tmpPenY = parseFloat(args[1]);
            trackPoint(tmpPenX, tmpPenY);
            break;
          case 'ARC': {
            const cx = parseFloat(args[0]), cy = parseFloat(args[1]), r = parseFloat(args[2]);
            trackPoint(cx - r, cy - r);
            trackPoint(cx + r, cy + r);
            break;
          }
          case 'SIZ': tmpSiz = parseFloat(args[0]); break;
          case 'ROT': tmpRot = parseFloat(args[0]); break;
          case 'SYM': {
            const id = parseInt(args[0]);
            const sx = parseFloat(args[1]), sy = parseFloat(args[2]);
            const def = defs[id];
            if (def) {
              for (const dc of def) {
                const da = dc.args.map(parseFloat);
                if (dc.cmd === 'MOV' || dc.cmd === 'LIN') {
                  let rx = da[0] * tmpSiz, ry = da[1] * tmpSiz;
                  if (tmpRot !== 0) {
                    const rad = tmpRot * Math.PI / 180;
                    const nx = rx * Math.cos(rad) - ry * Math.sin(rad);
                    const ny = rx * Math.sin(rad) + ry * Math.cos(rad);
                    rx = nx; ry = ny;
                  }
                  trackPoint(sx + rx, sy + ry);
                } else if (dc.cmd === 'ARC') {
                  let acx = da[0] * tmpSiz, acy = da[1] * tmpSiz;
                  const ar = da[2] * tmpSiz;
                  if (tmpRot !== 0) {
                    const rad = tmpRot * Math.PI / 180;
                    const nx = acx * Math.cos(rad) - acy * Math.sin(rad);
                    const ny = acx * Math.sin(rad) + acy * Math.cos(rad);
                    acx = nx; acy = ny;
                  }
                  trackPoint(sx + acx - ar, sy + acy - ar);
                  trackPoint(sx + acx + ar, sy + acy + ar);
                }
              }
            }
            tmpRot = 0;
            break;
          }
          case 'TXT': {
            const str = args[0];
            const rest = args.slice(1);
            const txtX = parseFloat(rest[0]), txtY = parseFloat(rest[1]);
            const tsz = parseFloat(rest[2]);
            const halfW = str.length * tsz * 0.3;
            trackPoint(txtX - halfW, txtY - tsz * 0.5);
            trackPoint(txtX + halfW, txtY + tsz * 0.5);
            break;
          }
        }
      }

      if (!isFinite(minX)) { minX = 0; minY = 0; maxX = 200; maxY = 200; }

      const pad = 15;
      minX -= pad; minY -= pad; maxX += pad; maxY += pad;
      const worldW = maxX - minX;
      const worldH = maxY - minY;
      const scaleX = canvas.width / worldW;
      const scaleY = canvas.height / worldH;
      const scale = Math.min(scaleX, scaleY);
      const offsetX = (canvas.width - worldW * scale) / 2 - minX * scale;
      const offsetY = (canvas.height - worldH * scale) / 2 - minY * scale;

      const tx = (x) => x * scale + offsetX;
      const ty = (y) => y * scale + offsetY;

      // ── Clear and draw ──
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      let penX = 0, penY = 0, siz = 1.0, rot = 0;
      ctx.lineWidth = Math.max(1, scale * 1.2);
      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';

      for (const { cmd, args } of sceneCmds) {
        switch (cmd) {
          case 'MOV':
            penX = parseFloat(args[0]);
            penY = parseFloat(args[1]);
            break;

          case 'LIN': {
            const lx = parseFloat(args[0]), ly = parseFloat(args[1]);
            const col = parseInt(args[2]);
            ctx.strokeStyle = palette[col] || '#000';
            ctx.beginPath();
            ctx.moveTo(tx(penX), ty(penY));
            ctx.lineTo(tx(lx), ty(ly));
            ctx.stroke();
            penX = lx; penY = ly;
            break;
          }

          case 'ARC': {
            const cx = parseFloat(args[0]), cy = parseFloat(args[1]);
            const r = parseFloat(args[2]);
            const startDeg = parseFloat(args[3]), endDeg = parseFloat(args[4]);
            const col = parseInt(args[5]);
            ctx.strokeStyle = palette[col] || '#000';
            ctx.beginPath();
            ctx.arc(tx(cx), ty(cy), r * scale, startDeg * Math.PI / 180, endDeg * Math.PI / 180);
            ctx.stroke();
            const endRad = endDeg * Math.PI / 180;
            penX = cx + r * Math.cos(endRad);
            penY = cy + r * Math.sin(endRad);
            break;
          }

          case 'SIZ':
            siz = parseFloat(args[0]);
            break;

          case 'ROT':
            rot = parseFloat(args[0]);
            break;

          case 'SYM': {
            const id = parseInt(args[0]);
            const sx = parseFloat(args[1]), sy = parseFloat(args[2]);
            const col = parseInt(args[3]);
            const def = defs[id];
            if (!def) break;

            ctx.strokeStyle = palette[col] || '#000';
            const rad = rot * Math.PI / 180;
            let localPenX = 0, localPenY = 0;

            for (const dc of def) {
              const da = dc.args.map(parseFloat);
              if (dc.cmd === 'MOV') {
                let rx = da[0] * siz, ry = da[1] * siz;
                if (rot !== 0) {
                  const nx = rx * Math.cos(rad) - ry * Math.sin(rad);
                  const ny = rx * Math.sin(rad) + ry * Math.cos(rad);
                  rx = nx; ry = ny;
                }
                localPenX = sx + rx;
                localPenY = sy + ry;
              } else if (dc.cmd === 'LIN') {
                let rx = da[0] * siz, ry = da[1] * siz;
                if (rot !== 0) {
                  const nx = rx * Math.cos(rad) - ry * Math.sin(rad);
                  const ny = rx * Math.sin(rad) + ry * Math.cos(rad);
                  rx = nx; ry = ny;
                }
                const ex = sx + rx, ey = sy + ry;
                ctx.beginPath();
                ctx.moveTo(tx(localPenX), ty(localPenY));
                ctx.lineTo(tx(ex), ty(ey));
                ctx.stroke();
                localPenX = ex; localPenY = ey;
              } else if (dc.cmd === 'ARC') {
                let acx = da[0] * siz, acy = da[1] * siz;
                const ar = da[2] * siz;
                if (rot !== 0) {
                  const nx = acx * Math.cos(rad) - acy * Math.sin(rad);
                  const ny = acx * Math.sin(rad) + acy * Math.cos(rad);
                  acx = nx; acy = ny;
                }
                const startDeg = da[3], endDeg = da[4];
                ctx.beginPath();
                ctx.arc(tx(sx + acx), ty(sy + acy), ar * scale,
                        (startDeg + rot) * Math.PI / 180,
                        (endDeg + rot) * Math.PI / 180);
                ctx.stroke();
                const endRad = (endDeg + rot) * Math.PI / 180;
                localPenX = sx + acx + ar * Math.cos(endRad);
                localPenY = sy + acy + ar * Math.sin(endRad);
              }
            }
            rot = 0;
            break;
          }

          case 'TXT': {
            const str = args[0];
            const rest = args.slice(1);
            const txtX = parseFloat(rest[0]), txtY = parseFloat(rest[1]);
            const txtSize = parseFloat(rest[2]);
            const col = parseInt(rest[3]);
            ctx.fillStyle = palette[col] || '#000';
            ctx.font = `${txtSize * scale}px sans-serif`;
            ctx.textBaseline = 'middle';
            ctx.textAlign = 'center';
            ctx.fillText(str, tx(txtX), ty(txtY));
            break;
          }
        }
      }

      return { defs: Object.keys(defs).length, commands: sceneCmds.length };
    }
  }

  // ─── VVDBinary ───────────────────────────────────────────────────────
  // Serialize parsed VVD commands to binary format and deserialize back.

  const OPCODES = {
    PAL: 0x01, DEF: 0x02, SIZ: 0x03, ROT: 0x04,
    MOV: 0x05, LIN: 0x06, ARC: 0x07, SYM: 0x08,
    TXT: 0x09, INC: 0x0A, END: 0xFF
  };

  const DEF_TAGS = { MOV: 0x00, LIN: 0x01, ARC: 0x02 };

  class VVDBinary {
    /**
     * Serialize parsed VVD data to binary ArrayBuffer.
     * @param {{ palette: Object, defs: Object, commands: Array }} parsed
     * @param {{ coordScale?: number }} options
     * @returns {ArrayBuffer}
     */
    serialize(parsed, options) {
      const coordScale = (options && options.coordScale) || 1;
      const buf = [];
      const view = {
        u8(v) { buf.push(v & 0xFF); },
        u16(v) { buf.push(v & 0xFF, (v >> 8) & 0xFF); },
        i16(v) {
          if (v < 0) v = 0x10000 + v;
          buf.push(v & 0xFF, (v >> 8) & 0xFF);
        },
        str(s) {
          buf.push(s.length & 0xFF);
          for (let i = 0; i < s.length; i++) buf.push(s.charCodeAt(i) & 0xFF);
        }
      };

      // Header: magic "VVD1", version 0x02, coordScale u16
      view.u8(0x56); view.u8(0x56); view.u8(0x44); view.u8(0x31); // VVD1
      view.u8(0x02); // version
      view.u16(coordScale);

      const cs = coordScale;

      // PAL entries
      for (const [id, hex] of Object.entries(parsed.palette)) {
        view.u8(OPCODES.PAL);
        view.u8(parseInt(id));
        const r = parseInt(hex.slice(1, 3), 16);
        const g = parseInt(hex.slice(3, 5), 16);
        const b = parseInt(hex.slice(5, 7), 16);
        view.u8(r); view.u8(g); view.u8(b);
      }

      // DEF blocks
      for (const [id, cmds] of Object.entries(parsed.defs)) {
        view.u8(OPCODES.DEF);
        view.u16(parseInt(id));
        view.u16(cmds.length);
        for (const dc of cmds) {
          const da = dc.args.map(parseFloat);
          if (dc.cmd === 'MOV') {
            view.u8(DEF_TAGS.MOV);
            view.i16(Math.round(da[0] * cs));
            view.i16(Math.round(da[1] * cs));
          } else if (dc.cmd === 'LIN') {
            view.u8(DEF_TAGS.LIN);
            view.i16(Math.round(da[0] * cs));
            view.i16(Math.round(da[1] * cs));
          } else if (dc.cmd === 'ARC') {
            view.u8(DEF_TAGS.ARC);
            view.i16(Math.round(da[0] * cs));
            view.i16(Math.round(da[1] * cs));
            view.u16(Math.round(da[2] * cs));
            view.i16(Math.round(da[3] * 10));
            view.i16(Math.round(da[4] * 10));
          }
        }
      }

      // Scene commands
      for (const { cmd, args } of parsed.commands) {
        switch (cmd) {
          case 'SIZ':
            view.u8(OPCODES.SIZ);
            view.u16(Math.round(parseFloat(args[0]) * 100));
            break;
          case 'ROT':
            view.u8(OPCODES.ROT);
            view.i16(Math.round(parseFloat(args[0]) * 10));
            break;
          case 'MOV':
            view.u8(OPCODES.MOV);
            view.i16(Math.round(parseFloat(args[0]) * cs));
            view.i16(Math.round(parseFloat(args[1]) * cs));
            break;
          case 'LIN':
            view.u8(OPCODES.LIN);
            view.i16(Math.round(parseFloat(args[0]) * cs));
            view.i16(Math.round(parseFloat(args[1]) * cs));
            view.u8(parseInt(args[2]));
            break;
          case 'ARC':
            view.u8(OPCODES.ARC);
            view.i16(Math.round(parseFloat(args[0]) * cs));
            view.i16(Math.round(parseFloat(args[1]) * cs));
            view.u16(Math.round(parseFloat(args[2]) * cs));
            view.i16(Math.round(parseFloat(args[3]) * 10));
            view.i16(Math.round(parseFloat(args[4]) * 10));
            view.u8(parseInt(args[5]));
            break;
          case 'SYM':
            view.u8(OPCODES.SYM);
            view.u16(parseInt(args[0]));
            view.i16(Math.round(parseFloat(args[1]) * cs));
            view.i16(Math.round(parseFloat(args[2]) * cs));
            view.u8(parseInt(args[3]));
            break;
          case 'TXT': {
            view.u8(OPCODES.TXT);
            const str = args[0];
            const rest = args.slice(1);
            view.str(str);
            view.i16(Math.round(parseFloat(rest[0]) * cs));
            view.i16(Math.round(parseFloat(rest[1]) * cs));
            view.u16(Math.round(parseFloat(rest[2]) * 10));
            view.u8(parseInt(rest[3]));
            break;
          }
          case 'INC': {
            view.u8(OPCODES.INC);
            view.str(args[0]);
            break;
          }
        }
      }

      // End marker
      view.u8(OPCODES.END);

      return new Uint8Array(buf).buffer;
    }

    /**
     * Deserialize binary VVD buffer to parsed representation.
     * @param {ArrayBuffer} buffer
     * @returns {{ palette: Object, defs: Object, commands: Array }}
     */
    deserialize(buffer) {
      const data = new DataView(buffer);
      let pos = 0;

      const u8 = () => data.getUint8(pos++);
      const u16 = () => { const v = data.getUint16(pos, true); pos += 2; return v; };
      const i16 = () => { const v = data.getInt16(pos, true); pos += 2; return v; };
      const str = () => {
        const len = u8();
        let s = '';
        for (let i = 0; i < len; i++) s += String.fromCharCode(u8());
        return s;
      };

      // Header
      const m0 = u8(), m1 = u8(), m2 = u8(), m3 = u8();
      if (m0 !== 0x56 || m1 !== 0x56 || m2 !== 0x44 || m3 !== 0x31) {
        throw new Error('Invalid VVD binary: bad magic number');
      }
      const version = u8();
      if (version !== 0x02) {
        throw new Error('Unsupported VVD binary version: ' + version);
      }
      const coordScale = u16();
      const cs = coordScale || 1;

      const palette = {};
      const defs = {};
      const commands = [];

      while (pos < data.byteLength) {
        const opcode = u8();
        if (opcode === 0xFF) break; // END marker

        switch (opcode) {
          case OPCODES.PAL: {
            const id = u8();
            const r = u8(), g = u8(), b = u8();
            const hex = '#' + [r, g, b].map(c => c.toString(16).padStart(2, '0')).join('');
            palette[id] = hex;
            break;
          }
          case OPCODES.DEF: {
            const id = u16();
            const count = u16();
            const defCmds = [];
            for (let j = 0; j < count; j++) {
              const tag = u8();
              if (tag === DEF_TAGS.MOV) {
                const x = (i16() / cs).toString(), y = (i16() / cs).toString();
                defCmds.push({ cmd: 'MOV', args: [x, y] });
              } else if (tag === DEF_TAGS.LIN) {
                const x = (i16() / cs).toString(), y = (i16() / cs).toString();
                defCmds.push({ cmd: 'LIN', args: [x, y] });
              } else if (tag === DEF_TAGS.ARC) {
                const cx = (i16() / cs).toString(), cy = (i16() / cs).toString();
                const r = (u16() / cs).toString();
                const sd = (i16() / 10).toString(), ed = (i16() / 10).toString();
                defCmds.push({ cmd: 'ARC', args: [cx, cy, r, sd, ed] });
              }
            }
            defs[id] = defCmds;
            break;
          }
          case OPCODES.SIZ:
            commands.push({ cmd: 'SIZ', args: [(u16() / 100).toString()] });
            break;
          case OPCODES.ROT:
            commands.push({ cmd: 'ROT', args: [(i16() / 10).toString()] });
            break;
          case OPCODES.MOV:
            commands.push({ cmd: 'MOV', args: [(i16() / cs).toString(), (i16() / cs).toString()] });
            break;
          case OPCODES.LIN: {
            const x = (i16() / cs).toString(), y = (i16() / cs).toString();
            const col = u8().toString();
            commands.push({ cmd: 'LIN', args: [x, y, col] });
            break;
          }
          case OPCODES.ARC: {
            const cx = (i16() / cs).toString(), cy = (i16() / cs).toString();
            const r = (u16() / cs).toString();
            const sd = (i16() / 10).toString(), ed = (i16() / 10).toString();
            const col = u8().toString();
            commands.push({ cmd: 'ARC', args: [cx, cy, r, sd, ed, col] });
            break;
          }
          case OPCODES.SYM: {
            const id = u16().toString();
            const x = (i16() / cs).toString(), y = (i16() / cs).toString();
            const col = u8().toString();
            commands.push({ cmd: 'SYM', args: [id, x, y, col] });
            break;
          }
          case OPCODES.TXT: {
            const s = str();
            const x = (i16() / cs).toString(), y = (i16() / cs).toString();
            const sz = (u16() / 10).toString();
            const col = u8().toString();
            commands.push({ cmd: 'TXT', args: [s, x, y, sz, col] });
            break;
          }
          case OPCODES.INC: {
            const fname = str();
            commands.push({ cmd: 'INC', args: [fname] });
            break;
          }
          default:
            throw new Error('Unknown opcode: 0x' + opcode.toString(16).padStart(2, '0') + ' at position ' + (pos - 1));
        }
      }

      return { palette, defs, commands };
    }
  }

  // ─── Exports ─────────────────────────────────────────────────────────
  return { VVDParser, VVDRenderer, VVDBinary };
});

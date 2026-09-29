from pathlib import Path

ORIGIN = 0x8000
code = bytearray()
labels = {}
branches = []
absolutes = []

def pc():
    return ORIGIN + len(code)

def label(name):
    labels[name] = pc()

def emit(*values):
    code.extend(values)

def branch(opcode, target):
    emit(opcode, 0)
    branches.append((len(code) - 1, target))

def jump(opcode, target):
    emit(opcode, 0, 0)
    absolutes.append((len(code) - 2, target))

# Zero page: $00 player x, $01 target x, $02 score.
label("start")
emit(0xA9, 10, 0x85, 0x00)                 # lda #10 / sta $00
emit(0xA9, 3, 0x85, 0x01)                  # lda #3 / sta $01
emit(0xA9, 0, 0x85, 0x02)                  # score = 0
label("frame")
emit(0xA2, 0)                              # ldx #0
emit(0xA9, 0x20)                           # lda #space
label("clear")
emit(0x9D, 0x00, 0x1E, 0xE8, 0xE0, 22)    # sta $1e00,x / inx / cpx #22
branch(0xD0, "clear")
emit(0xA6, 0x00, 0xA9, 0x01, 0x9D, 0x00, 0x1E)  # draw player
emit(0xA6, 0x01, 0xA9, 0x02, 0x9D, 0x00, 0x1E)  # draw target

emit(0xAD, 0x11, 0x91, 0x29, 0x01)         # left?
branch(0xF0, "no_left")
emit(0xA5, 0x00)
branch(0xF0, "no_left")
emit(0xC6, 0x00)
label("no_left")

emit(0xAD, 0x11, 0x91, 0x29, 0x02)         # right?
branch(0xF0, "no_right")
emit(0xA5, 0x00, 0xC9, 21)
branch(0xB0, "no_right")
emit(0xE6, 0x00)
label("no_right")

emit(0xAD, 0x11, 0x91, 0x29, 0x04)         # A resets score
branch(0xF0, "no_reset")
emit(0xA9, 0, 0x85, 0x02)
label("no_reset")

emit(0xA5, 0x00, 0xC5, 0x01)               # caught target?
branch(0xD0, "trap")
emit(0xE6, 0x02, 0x18, 0xA5, 0x01, 0x69, 7, 0xC9, 22)
branch(0x90, "save_target")
emit(0xE9, 22)                              # CMP left carry set
label("save_target")
emit(0x85, 0x01)
label("trap")
jump(0x4C, "host_trap")

for offset, target in branches:
    target_addr = labels[target]
    next_addr = ORIGIN + offset + 1
    delta = target_addr - next_addr
    if not -128 <= delta <= 127:
        raise ValueError((target, delta))
    code[offset] = delta & 0xFF

host_trap = 0x9000
labels["host_trap"] = host_trap
for offset, target in absolutes:
    address = labels[target]
    code[offset] = address & 0xFF
    code[offset + 1] = address >> 8

if pc() > host_trap:
    raise ValueError("program overlaps host trap")
code.extend(bytes(host_trap - pc()))
code.extend((0x4C, labels["frame"] & 0xFF, labels["frame"] >> 8))

output = Path(__file__).resolve().parents[1] / "build" / "test-content" / "vic20-catch.prg"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_bytes(bytes((ORIGIN & 0xFF, ORIGIN >> 8)) + code)
print(f"{output} bytes={output.stat().st_size} start={labels['start']:04x} trap={host_trap:04x}")

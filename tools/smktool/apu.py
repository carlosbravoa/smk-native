"""A stand-in for the SPC700, good enough to stop the 65816 blocking on it.

We do not emulate the sound CPU.  The native port's audio is pre-recorded,
so the SPC700 is never needed to make sound - it is needed only because the
65816 *waits* for it, and those waits are pure control flow.

Two layers:

1. **The IPL boot protocol**, which is fixed hardware behaviour and fully
   documented, so it can be modelled exactly:
       - idle: ports 0/1 read $AA/$BB ("ready")
       - the CPU writes a destination to ports 2/3, a non-zero byte to
         port 1, then $CC to port 0 to start a block
       - the IPL echoes port 0; thereafter each byte goes to port 1 with an
         incrementing counter in port 0, echoed back
       - a block ending with port 1 == 0 transfers control to the uploaded
         driver
   No SPC700 code runs here; this is the ROM's *behaviour*, reimplemented.

2. **The driver protocol** afterwards, which is game-specific and not
   documented anywhere.  We log it and satisfy the waits we have identified.
   Anything unrecognised is reported rather than silently faked, so an
   unknown wait shows up as a diagnosis instead of a hang.
"""
from __future__ import annotations


class APU:
    IDLE, TRANSFER = 0, 1

    def __init__(self):
        self.port = [0xAA, 0xBB, 0x00, 0x00]   # what the 65816 reads
        self.inp = [0, 0, 0, 0]                # what it last wrote
        self.state = self.IDLE
        self.expect = 0
        self.blocks = 0
        self.bytes = 0
        self.log: list[tuple[str, int, int]] = []
        self.trace = False
        # After the driver is running the game sends it commands; echoing
        # port 0 keeps every "wait until the driver acknowledges" loop moving.
        self.driver_running = False
        self.uploads = 0
        self.pending_ready = False
        self.commands: list[tuple[int, int]] = []   # (port1, port0) sent to the driver
        self.reads_since_write = 0
        # How many consecutive reads of port 0 with no intervening write we
        # take to mean "the game is waiting for the IPL to say it is ready
        # again", i.e. it wants to upload another bank.
        self.ready_after_reads = 64
        # The upload is the game's own sound driver and music data.  We are
        # not running it, but we can record exactly what it would have been
        # written into, which is the whole content of an .spc dump.
        self.ram = bytearray(0x10000)
        self.entry = 0
        self.addr = 0

    # ---- the 65816 side ----
    #
    # Observed conversation (docs/NOTES.md 020), which is the documented IPL
    # protocol with one wrinkle worth naming: the byte written to port 1
    # before the kick is *both* the "data follows" flag and data byte 0, so
    # it must be non-zero.
    #
    #   P2=lo P3=hi   destination address
    #   P1=d0         first data byte (non-zero)
    #   P0=$CC        kick; IPL echoes $CC
    #   P0=$00        commits d0; IPL echoes 00
    #   P1=d1 P0=$01  ... and so on, IPL echoing the counter
    #   P2/P3=entry P1=$00 P0=counter+2   ends the block and runs the driver

    def read(self, addr: int) -> int:
        p = addr & 3
        v = self.port[p]
        if self.trace:
            self.log.append(("r", p, v))
        if self.pending_ready and p == 0:
            # the CPU has now seen the echo that ended the last block; on
            # hardware the IPL would have jumped to the driver.  We have no
            # driver, so go back to advertising "ready" for the next upload.
            self.port[0], self.port[1] = 0xAA, 0xBB
            self.pending_ready = False
        return v

    def write(self, addr: int, val: int) -> None:
        p = addr & 3
        val &= 0xFF
        self.inp[p] = val
        self.reads_since_write = 0
        if self.trace:
            self.log.append(("w", p, val))

        if p != 0:
            self.port[p] = val if p >= 2 else self.port[p]
            return

        if self.state == self.IDLE:
            # Only the $CC kick is echoed while idle.  Everything else the
            # game writes here is a command to its driver - and since the
            # driver's answer to the one we care about ("reset") is to jump
            # back into the IPL, the right reply is to keep advertising
            # "ready" rather than to echo.  That is what lets the game
            # upload a second sound bank; without it it polls $AA/$BB
            # forever and the whole title sequence stalls.
            if val == 0xCC and self.inp[1] != 0:
                self.state = self.TRANSFER
                self.expect = 0
                self.blocks += 1
                self.port[0] = val
                # the header put the destination in ports 2/3 and the first
                # data byte in port 1
                self.addr = self.inp[2] | self.inp[3] << 8
                self.ram[self.addr] = self.inp[1]
                self.addr = (self.addr + 1) & 0xFFFF
            else:
                # A command to the running driver.  We keep advertising
                # "ready" rather than echoing.
                #
                # Echoing was tried, on the theory that the race countdown is
                # sequenced against the sound driver and needs an
                # acknowledgement.  It did NOT release the countdown, and it
                # broke the re-upload path (one upload of 7 blocks instead of
                # two of 8), because the game then never sees $AA/$BB when it
                # wants to send the next sound bank.  See docs/NOTES.md 031.
                self.commands.append((self.inp[1], val))
                self.port[0], self.port[1] = 0xAA, 0xBB
            return

        # inside a block every write to port 0 is echoed; that is the handshake
        self.port[0] = val

        if val == self.expect:                    # a data byte was committed
            self.expect = (self.expect + 1) & 0xFF
            self.bytes += 1
            if self.expect != 1:                  # port 1 holds this byte
                self.ram[self.addr] = self.inp[1]
                self.addr = (self.addr + 1) & 0xFFFF
            return

        # any other value ends the block
        if self.inp[1] == 0:
            self.entry = self.inp[2] | self.inp[3] << 8
            self._finish()
        else:
            self.blocks += 1
            self.expect = 0
            self.addr = self.inp[2] | self.inp[3] << 8
            self.ram[self.addr] = self.inp[1]
            self.addr = (self.addr + 1) & 0xFFFF

    def _finish(self) -> None:
        """The block whose port-1 flag was zero: the IPL hands control to the
        uploaded driver.  We have no driver, so we go back to advertising
        "ready" - the game re-runs this upload routine for later banks and
        that path waits for $AA/$BB again."""
        self.state = self.IDLE
        self.driver_running = True
        self.uploads += 1
        # Do NOT clobber port 0 here: the CPU is still waiting to read back
        # the value it just wrote.  Advertise "ready" only once it has seen
        # that echo (see read()).
        self.pending_ready = True

    def summary(self) -> str:
        return (f"uploads={self.uploads} blocks={self.blocks} "
                f"bytes={self.bytes} running={self.driver_running}")


def write_spc(apu: "APU", path: str, pc: int | None = None) -> None:
    """Write a standard .spc dump of the driver the game uploaded.

    An .spc is just the SPC700's 64 KB RAM plus its registers, which is
    exactly what we captured from the upload protocol - no SPC700 emulation
    needed to produce one.  Any SPC player can then render it to audio
    locally, from the user's own ROM, so no copyrighted audio is ever
    shipped by this project.

    Caveat, stated plainly: this is the state *immediately after upload*.
    The driver has not run, the S-DSP registers are therefore all zero, and
    the driver is waiting for a "play track N" command on its ports.  Making
    a dump that starts playing a chosen track needs that command, which is
    game-specific and only partly observed so far.
    """
    buf = bytearray(0x10200)
    hdr = b"SNES-SPC700 Sound File Data v0.30"
    buf[0:len(hdr)] = hdr
    buf[0x21] = 26
    buf[0x22] = 26
    buf[0x23] = 27          # no ID666 tag
    buf[0x24] = 30          # version minor
    entry = apu.entry if pc is None else pc
    buf[0x25] = entry & 0xFF
    buf[0x26] = (entry >> 8) & 0xFF
    buf[0x27] = 0           # A
    buf[0x28] = 0           # X
    buf[0x29] = 0           # Y
    buf[0x2A] = 0           # PSW
    buf[0x2B] = 0xEF        # SP
    buf[0x100:0x10100] = apu.ram
    # 0x10100..0x10180 DSP registers, all zero: the driver programs them
    with open(path, "wb") as f:
        f.write(buf)

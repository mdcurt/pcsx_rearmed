# Link cable (SIO1) tests

Nothing here needs a console, a BIOS or a game.

| Test | What it checks | Run |
| --- | --- | --- |
| `test_model` | The port model (`libpcsxcore/sio1.c`) against the behaviour documented on the psx-spx "Serial Interfaces (SIO)" page, one check per statement. Two ports joined by an ideal cable, stepped in emulated time. | `make check` |
| `test_link` | The two process link (`libpcsxcore/sio1_link.c`). Two scripted consoles hammer their ports with pseudo-random accesses that depend on what they read. The linked pair has to produce exactly what a single process reference produces, however the host schedules the two processes. | `make check` |
| `test_link left` | What DSR and CTS read before the other console is there, while it is, and after it has left. | `make check` |
| `psx/linktest` | A program for the real machine: one console sends bytes, the other sends them back inverted; the round trip is timed with the console's own timer. Run here in two linked instances of the libretro core, started together and with console A started late, and in one instance with nothing connected. | `make check-emu CORE=...` |

`psx/linktest.exe` uses no BIOS calls and no interrupts, so it can be
started as it is on two real consoles joined by a link cable (role in
`0x80100200`, result at `0x80100000`, see the top of `psx/linktest.c`).
What it reports there is the reference the emulation still lacks.

With no cable (the option on and no other console, or the other console
gone) DSR and CTS read as on, as the fixed SIO_STAT value of the emulator
without the port has them. That is taken from what games need to see, not
measured: role 5 of `psx/linktest.exe` on a console with nothing in its
port would tell (`sr0` in the result).

The round trip times of two linked instances are the same on every run
for the same point at which the second console joins. Started at different
moments, as two consoles switched on by hand, they differ by a timer tick
here and there.

`retro_host` is a minimal libretro frontend without video or sound. As root
the lightrec builds map the console's RAM at address 0; `retro_host` keeps
that page occupied, since the savestate code does not cope with it.

Savestates: the port's own state survives a round trip in the middle of a
transfer (`HOST_STATE_AT=<frame>` with the loopback plug). The emulated
timing after a round trip is not always identical to a run without one,
which is the case without the port too (role 4 with the port off).

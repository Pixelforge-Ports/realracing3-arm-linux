# PortMaster package sources

What `../package_portmaster.sh` copies into the release zip.

```text
Real Racing 3.sh          the launcher, extracted to ports/ on the device
realracing3/              everything else, extracted to ports/realracing3/
  port.json               harbourmaster metadata (min_glibc, reqs, arch)
  gameinfo.xml            EmulationStation entry
  realracing3.gptk        gptokeyb config, every control unbound on purpose
  README.md CREDITS.md    user-facing docs
  PUT_REAL_RACING_3_DATA_HERE.txt
```

The loader binary, `libs.armhf/` and the licence files are added by the
packaging script from `../build/`; they are not checked in.

The launcher is not a stub: it does the Mali blob shim, audio routing, BYOG
data validation, gptokeyb and `pm_finish`. Read the comments before changing
anything — each block is there because its absence broke a sibling port.

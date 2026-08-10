/*
 * The port's release version — the single place it is written down.
 *
 * A field report is only actionable if it says which build produced it. The
 * sibling port's first field log on its GL provider preflight was
 * byte-identical to a log from the release before the preflight existed, and
 * there was no way to tell whether the user was running the new release or an
 * old one still sitting on the SD card. This port has the same problem for the
 * same reason: its log opens with the run banner and names no build.
 *
 * So the version is defined here and nowhere else:
 *
 *   - the loader prints it as its first trace line, and answers --version;
 *   - the launcher asks the binary (`realracing3 --version`) instead of
 *     carrying its own copy of the string, so a stale launcher cannot claim a
 *     version the binary is not;
 *   - the packager reads this header when it reports what it built.
 *
 * Bump it here when cutting a release; nothing else needs editing.
 */
#ifndef REALRACING3_PORT_VERSION_H
#define REALRACING3_PORT_VERSION_H

#define REALRACING3_PORT_VERSION "1.0.3"

#endif /* REALRACING3_PORT_VERSION_H */

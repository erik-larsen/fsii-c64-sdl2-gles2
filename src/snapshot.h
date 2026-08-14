/*
 * snapshot.h - load the VICE-dumped machine state into the c64 model.
 */
#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include "c64.h"

/*
 * Loads <base> (cpu view), <base>.ram (raw RAM, optional) and
 * <base>.regs (register text file) produced by tools/vice_dump.py.
 * Returns 0 on success.
 */
int snapshot_boot(struct c64 *m, const char *base);

#endif

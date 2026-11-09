// rr_gfxpack.h - Ring Racers E: ANGLE / Mesa DLLs embedded in the .exe (see rr_gfxpack.cpp).
#ifndef RR_GFXPACK_H
#define RR_GFXPACK_H

#ifdef __cplusplus
extern "C" {
#endif

// 1 = the .exe carries this file of the group ("angle" or "mesa")
int RRPack_Has(const char *group, const char *name);
// Writes the group's DLLs to %LOCALAPPDATA%\RingRacersE\<group>-<id>\ (only the missing ones) and returns
// that folder in out. 0 = nothing embedded or the files could not be written.
int RRPack_Extract(const char *group, char *out, int n);

#ifdef __cplusplus
}
#endif

#endif

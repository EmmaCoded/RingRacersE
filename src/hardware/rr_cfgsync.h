// Ring Racers E: its own config file (ringconfigE.cfg), kept in sync with the original Ring Racers config.
#ifndef __RR_CFGSYNC_H__
#define __RR_CFGSYNC_H__

#ifdef __cplusplus
extern "C" {
#endif

/// `configfile` comes in with the path of ringconfig.cfg (the original game's) and leaves with ringconfigE.cfg.
/// First it syncs the NON-graphics options between both files (the most recently modified file wins).
/// Graphics options (renderer, gfxapi, resolution, gamma, gr_*...) are never copied.
void RR_CfgSync(char *configfile, unsigned long size);

#ifdef __cplusplus
}
#endif

#endif

//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 1993-2008 Raven Software
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// R_planes.c

#include <math.h>
#include <stdlib.h>
#include "doomdef.h"
#include "deh_str.h"
#include "i_system.h"
#include "r_local.h"
// [CDVK] angle_t (binary angular measure) -> radians, exact over the
// full 32-bit range.  Used by the exact floor/ceiling span mapping.
#define ANGTODBL(a) ((double)(a) * (2.0 * 3.14159265358979323846 / 4294967296.0))

planefunction_t floorfunc, ceilingfunc;

//
// sky mapping
//
int skyflatnum;
int skytexture;
int skytexturemid;
fixed_t skyiscale;

//
// opening
//

visplane_t visplanes[MAXVISPLANES], *lastvisplane;
visplane_t *floorplane, *ceilingplane;

short openings[MAXOPENINGS], *lastopening;

//
// clip values are the solid pixel bounding the range
// floorclip starts out SCREENHEIGHT
// ceilingclip starts out -1
//
short floorclip[SCREENWIDTH];
short ceilingclip[SCREENWIDTH];

//
// spanstart holds the start of a plane span
// initialized to 0 at start
//
int spanstart[SCREENHEIGHT];
int spanstop[SCREENHEIGHT];

//
// texture mapping
//
lighttable_t **planezlight;
fixed_t planeheight;

fixed_t yslope[SCREENHEIGHT];
fixed_t distscale[SCREENWIDTH];
fixed_t basexscale, baseyscale;

fixed_t cachedheight[SCREENHEIGHT];
fixed_t cacheddistance[SCREENHEIGHT];
fixed_t cachedxstep[SCREENHEIGHT];
fixed_t cachedystep[SCREENHEIGHT];


/*
================
=
= R_InitSkyMap
=
= Called whenever the view size changes
=
================
*/

void R_InitSkyMap(void)
{
    skyflatnum = R_FlatNumForName(DEH_String("F_SKY1"));
    skytexturemid = 200 * FRACUNIT;
    skyiscale = FRACUNIT;
}


/*
====================
=
= R_InitPlanes
=
= Only at game startup
====================
*/

void R_InitPlanes(void)
{
}


/*
================
=
= R_MapPlane
=
global vars:

planeheight
ds_source
basexscale
baseyscale
viewx
viewy

BASIC PRIMITIVE
================
*/

void R_MapPlane(int y, int x1, int x2)
{
    fixed_t distance;
    unsigned index;

#ifdef RANGECHECK
    if (x2 < x1 || x1 < 0 || x2 >= viewwidth || (unsigned) y > viewheight)
        I_Error("R_MapPlane: %i, %i at %i", x1, x2, y);
#endif

    if (planeheight != cachedheight[y])
    {
        cachedheight[y] = planeheight;
        distance = cacheddistance[y] = FixedMul(planeheight, yslope[y]);

        ds_xstep = cachedxstep[y] = FixedMul(distance, basexscale);
        ds_ystep = cachedystep[y] = FixedMul(distance, baseyscale);
    }
    else
    {
        distance = cacheddistance[y];
        ds_xstep = cachedxstep[y];
        ds_ystep = cachedystep[y];
    }

    // [CDVK] exact span origin: trace the true ray of column x1 onto
    // the plane instead of the 8192-entry fine tables.  The per-row
    // step (FixedMul(distance, basexscale/baseyscale)) is exact for
    // planes, so the whole row maps exactly and floors/ceilings
    // stop swimming when moving or turning.
    // (x1-centerx) is a raw pixel delta: shift to fixed point
    // before dividing by the fixed_t projection, or u is 65536x
    // too small and every row starts on the center ray.
    // len is the true horizontal ray length for the row (distance
    // is the range along the row's center ray; every point of the
    // row line sits distance*sqrt(1+u^2) from the view).  The view
    // offset is wrapped int32 exactly like vanilla fixed-point
    // math: the span blitter masks texels to 6 bits, so wrap-around
    // is harmless, while clamping would pin far/map-edge spans to
    // a wrong texel phase, and converting an out-of-range double
    // to int32 is undefined.  |offset| < 2^18*2^16 < 2^34, so the
    // int64 conversion is safe.
    {
        double u = (double)((x1 - centerx) << FRACBITS) / (double)projection;
        double ray = ANGTODBL(viewangle) - atan(u);
        double len = (double)distance / 65536.0 * sqrt(1.0 + u * u);

        ds_xfrac = viewx + (fixed_t)(int64_t)(cos(ray) * len * 65536.0);
        ds_yfrac = -viewy - (fixed_t)(int64_t)(sin(ray) * len * 65536.0);
    }

    if (fixedcolormap)
        ds_colormap = fixedcolormap;
    else
    {
        index = distance >> LIGHTZSHIFT;
        if (index >= MAXLIGHTZ)
            index = MAXLIGHTZ - 1;
        ds_colormap = planezlight[index];
    }

    ds_y = y;
    ds_x1 = x1;
    ds_x2 = x2;

    spanfunc();                 // high or low detail
}

//=============================================================================

/*
====================
=
= R_ClearPlanes
=
= At begining of frame
====================
*/

void R_ClearPlanes(void)
{
    int i;

//
// opening / clipping determination
//      
    for (i = 0; i < viewwidth; i++)
    {
        floorclip[i] = viewheight;
        ceilingclip[i] = -1;
    }

    lastvisplane = visplanes;
    lastopening = openings;

//
// texture calculation
//
    memset(cachedheight, 0, sizeof(cachedheight));
    // [CDVK] exact sin/cos of the view direction over the focal
    // length, instead of the fine-table lookup over centerxfrac:
    // floors and ceilings stop wobbling while turning (R_MapPlane).
    {
        double s, c;

        s = sin(ANGTODBL(viewangle - ANG90));
        c = cos(ANGTODBL(viewangle - ANG90));
        // [CDVK] round half away from zero: plain truncation biased the
        // step by up to a full 16.16 lsb, letting far floor/ceiling rows
        // drift up to half a texel per pixel across wide spans.
        basexscale = (fixed_t) (c * 4294967296.0 / projection
                                + (c < 0 ? -0.5 : 0.5));
        baseyscale = -(fixed_t) (s * 4294967296.0 / projection
                                 + (s < 0 ? -0.5 : 0.5));
    }
}



/*
===============
=
= R_FindPlane
=
===============
*/

visplane_t *R_FindPlane(fixed_t height, int picnum,
                        int lightlevel, int special)
{
    visplane_t *check;

    if (picnum == skyflatnum)
    {
        // all skies map together
        height = 0;
        lightlevel = 0;
    }

    for (check = visplanes; check < lastvisplane; check++)
    {
        if (height == check->height
            && picnum == check->picnum
            && lightlevel == check->lightlevel && special == check->special)
            break;
    }

    if (check < lastvisplane)
    {
        return (check);
    }

    if (lastvisplane - visplanes == MAXVISPLANES)
    {
        I_Error("R_FindPlane: no more visplanes");
    }

    lastvisplane++;
    check->height = height;
    check->picnum = picnum;
    check->lightlevel = lightlevel;
    check->special = special;
    check->minx = SCREENWIDTH;
    check->maxx = -1;
    memset(check->top, 0xff, sizeof(check->top));
    return (check);
}

/*
===============
=
= R_CheckPlane
=
===============
*/

visplane_t *R_CheckPlane(visplane_t * pl, int start, int stop)
{
    int intrl, intrh;
    int unionl, unionh;
    int x;

    if (start < pl->minx)
    {
        intrl = pl->minx;
        unionl = start;
    }
    else
    {
        unionl = pl->minx;
        intrl = start;
    }

    if (stop > pl->maxx)
    {
        intrh = pl->maxx;
        unionh = stop;
    }
    else
    {
        unionh = pl->maxx;
        intrh = stop;
    }

    for (x = intrl; x <= intrh; x++)
        if (pl->top[x] != 0xff)
            break;

    if (x > intrh)
    {
        pl->minx = unionl;
        pl->maxx = unionh;
        return pl;              // use the same one
    }

// make a new visplane

    lastvisplane->height = pl->height;
    lastvisplane->picnum = pl->picnum;
    lastvisplane->lightlevel = pl->lightlevel;
    lastvisplane->special = pl->special;
    pl = lastvisplane++;
    pl->minx = start;
    pl->maxx = stop;
    memset(pl->top, 0xff, sizeof(pl->top));

    return pl;
}



//=============================================================================

/*
================
=
= R_MakeSpans
=
================
*/

void R_MakeSpans(int x, int t1, int b1, int t2, int b2)
{
    while (t1 < t2 && t1 <= b1)
    {
        R_MapPlane(t1, spanstart[t1], x - 1);
        t1++;
    }
    while (b1 > b2 && b1 >= t1)
    {
        R_MapPlane(b1, spanstart[b1], x - 1);
        b1--;
    }

    while (t2 < t1 && t2 <= b2)
    {
        spanstart[t2] = x;
        t2++;
    }
    while (b2 > b1 && b2 >= t2)
    {
        spanstart[b2] = x;
        b2--;
    }
}



/*
================
=
= R_DrawPlanes
=
= At the end of each frame
================
*/

void R_DrawPlanes(void)
{
    visplane_t *pl;
    int light;
    int x, stop;
    int lumpnum;
    int angle;
    byte *tempSource;

    byte *dest;
    int count;
    fixed_t frac, fracstep;

#ifdef RANGECHECK
    if (ds_p - drawsegs > MAXDRAWSEGS)
        I_Error("R_DrawPlanes: drawsegs overflow (%td)",
                ds_p - drawsegs);
    if (lastvisplane - visplanes > MAXVISPLANES)
        I_Error("R_DrawPlanes: visplane overflow (%td)",
                lastvisplane - visplanes);
    if (lastopening - openings > MAXOPENINGS)
        I_Error("R_DrawPlanes: opening overflow (%td)",
                lastopening - openings);
#endif

    for (pl = visplanes; pl < lastvisplane; pl++)
    {
        if (pl->minx > pl->maxx)
            continue;
        //
        // sky flat
        //
        if (pl->picnum == skyflatnum)
        {
            dc_iscale = skyiscale;
            dc_colormap = colormaps;    // sky is allways drawn full bright
            dc_texturemid = skytexturemid;
            for (x = pl->minx; x <= pl->maxx; x++)
            {
                dc_yl = pl->top[x];
                dc_yh = pl->bottom[x];
                if (dc_yl <= dc_yh)
                {
                    angle = (viewangle + xtoviewangle[x]) >> ANGLETOSKYSHIFT;
                    dc_x = x;
                    dc_source = R_GetColumn(skytexture, angle);

                    count = dc_yh - dc_yl;
                    if (count < 0)
                        return;

#ifdef RANGECHECK
                    if ((unsigned) dc_x >= SCREENWIDTH || dc_yl < 0
                        || dc_yh >= SCREENHEIGHT)
                        I_Error("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh,
                                dc_x);
#endif

                    dest = ylookup[dc_yl] + columnofs[dc_x];

                    fracstep = 1;
                    frac = (dc_texturemid >> FRACBITS) + (dc_yl - centery);
                    do
                    {
                        *dest = dc_source[frac];
                        dest += SCREENWIDTH;
                        frac += fracstep;
                    }
                    while (count--);

//                                      colfunc ();
                }
            }
            continue;
        }

        //
        // regular flat
        //
        lumpnum = firstflat + flattranslation[pl->picnum];

        tempSource = W_CacheLumpNum(lumpnum, PU_STATIC);

        switch (pl->special)
        {
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:           // Scroll_North
                ds_source = tempSource;
                break;
            case 20:
            case 21:
            case 22:
            case 23:
            case 24:           // Scroll_East
                ds_source = tempSource + ((63 - ((leveltime >> 1) & 63)) <<
                                          (pl->special - 20) & 63);
                //ds_source = tempSource+((leveltime>>1)&63);
                break;
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:           // Scroll_South
                ds_source = tempSource;
                break;
            case 35:
            case 36:
            case 37:
            case 38:
            case 39:           // Scroll_West
                ds_source = tempSource;
                break;
            case 4:            // Scroll_EastLavaDamage
                ds_source =
                    tempSource + (((63 - ((leveltime >> 1) & 63)) << 3) & 63);
                break;
            default:
                ds_source = tempSource;
        }
        planeheight = abs(pl->height - viewz);
        light = (pl->lightlevel >> LIGHTSEGSHIFT) + extralight;
        if (light >= LIGHTLEVELS)
            light = LIGHTLEVELS - 1;
        if (light < 0)
            light = 0;
        planezlight = zlight[light];

        pl->top[pl->maxx + 1] = 0xff;
        pl->top[pl->minx - 1] = 0xff;

        stop = pl->maxx + 1;
        for (x = pl->minx; x <= stop; x++)
            R_MakeSpans(x, pl->top[x - 1], pl->bottom[x - 1], pl->top[x],
                        pl->bottom[x]);

        W_ReleaseLumpNum(lumpnum);
    }
}

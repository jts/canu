
/******************************************************************************
 *
 *  This file is part of canu, a software program that assembles whole-genome
 *  sequencing reads into contigs.
 *
 *  This software is based on:
 *    'Celera Assembler' (http://wgs-assembler.sourceforge.net)
 *    the 'kmer package' (http://kmer.sourceforge.net)
 *  both originally distributed by Applera Corporation under the GNU General
 *  Public License, version 2.
 *
 *  Canu branched from Celera Assembler at its revision 4587.
 *  Canu branched from the kmer project at its revision 1994.
 *
 *  This file is derived from:
 *
 *    src/AS_BAT/AS_BAT_SplitDiscontinuous.C
 *
 *  Modifications by:
 *
 *    Brian P. Walenz from 2010-NOV-23 to 2013-AUG-01
 *      are Copyright 2010-2013 J. Craig Venter Institute, and
 *      are subject to the GNU General Public License version 2
 *
 *    Brian P. Walenz from 2014-DEC-19 to 2015-MAR-03
 *      are Copyright 2014-2015 Battelle National Biodefense Institute, and
 *      are subject to the BSD 3-Clause License
 *
 *    Brian P. Walenz beginning on 2016-JAN-11
 *      are a 'United States Government Work', and
 *      are released in the public domain
 *
 *  File 'README.licenses' in the root directory of this distribution contains
 *  full conditions and disclaimers for each license.
 */

#include "AS_BAT_BestOverlapGraph.H"
#include "AS_BAT_Logging.H"

#include "AS_BAT_Unitig.H"


static
void
makeNewUnitig(TigVector    &tigs,
              uint32        splitReadsLen,
              ufNode       *splitReads) {
  Unitig *dangler = tigs.newUnitig(false);

  if (logFileFlagSet(LOG_SPLIT_DISCONTINUOUS))
    writeLog("splitDiscontinuous()--   new tig " F_U32 " with " F_U32 " reads (starting at read " F_U32 ").\n",
            dangler->id(), splitReadsLen, splitReads[0].ident);

  int splitOffset = -MIN(splitReads[0].position.bgn, splitReads[0].position.end);

  //  This should already be true, but we force it still
  splitReads[0].contained = 0;

  for (uint32 i=0; i<splitReadsLen; i++)
    dangler->addRead(splitReads[i], splitOffset, false);  //logFileFlagSet(LOG_SPLIT_DISCONTINUOUS));
}




//  After splitting and ejecting some contains, check for discontinuous tigs.
//
void splitDiscontinuous(TigVector &tigs, uint32 minOverlap) {
  uint32                numTested  = 0;
  uint32                numSplit   = 0;
  uint32                numCreated = 0;

  uint32                splitReadsLen = 0;
  uint32                splitReadsMax = 0;
  ufNode               *splitReads    = NULL;

  for (uint32 ti=0; ti<tigs.size(); ti++) {
    Unitig  *tig = tigs[ti];

    if ((tig == NULL) || (tig->ufpath.size() < 2))
      continue;

    //  Unitig must be sorted.  Someone upstream os screwing this up.
    tig->sort();

    //  We'll want to build an array of new reads to split out.  This can be up
    //  to the size of the largest unitig.
    splitReadsMax = MAX(splitReadsMax, tig->ufpath.size());

    //  Check that the unitig starts at position zero.  Not critical for the next loop, but
    //  needs to be dome sometime.
    int32   minPos = MIN(tig->ufpath[0].position.bgn, tig->ufpath[0].position.end);

    if (minPos == 0)
      continue;

    writeLog("splitDiscontinuous()-- tig " F_U32 " offset messed up; reset by " F_S32 ".\n", tig->id(), minPos);

    for (uint32 fi=0; fi<tig->ufpath.size(); fi++) {
      ufNode  *frg = &tig->ufpath[fi];

      frg->position.bgn -= minPos;
      frg->position.end -= minPos;
    }
  }

  splitReads = new ufNode [splitReadsMax];

  //  Now, finally, we can check for gaps in tigs.

  for (uint32 ti=0; ti<tigs.size(); ti++) {
    Unitig  *tig = tigs[ti];

    if ((tig == NULL) || (tig->ufpath.size() < 2))
      continue;

    //  We don't expect many tigs to be broken, so we'll do a first quick pass to just
    //  test if it is.

    int32  maxEnd   = MAX(tig->ufpath[0].position.bgn, tig->ufpath[0].position.end);
    bool   isBroken = false;

    for (uint32 fi=0; fi<tig->ufpath.size(); fi++) {
      ufNode  *frg = &tig->ufpath[fi];

      int32    bgn = MIN(frg->position.bgn, frg->position.end);
      int32    end = MAX(frg->position.bgn, frg->position.end);

      if (bgn > maxEnd - minOverlap) {
        isBroken = true;
        break;
      }

      maxEnd = MAX(maxEnd, end);
    }

    numTested++;

    if (isBroken == false)
      continue;

    numSplit++;

    //  Dang, busted unitig.  Fix it up.

    splitReadsLen = 0;
    maxEnd        = 0;

    if (logFileFlagSet(LOG_SPLIT_DISCONTINUOUS))
      writeLog("splitDiscontinuous()-- discontinuous tig " F_U32 " with " F_SIZE_T " reads broken into:\n",
              tig->id(), tig->ufpath.size());

    for (uint32 fi=0; fi<tig->ufpath.size(); fi++) {
      ufNode  *frg = &tig->ufpath[fi];

      int32    bgn = MIN(frg->position.bgn, frg->position.end);
      int32    end = MAX(frg->position.bgn, frg->position.end);

      //  Good thick overlap exists to this read, save it.
      if (bgn <= maxEnd - minOverlap) {
        assert(splitReadsLen < splitReadsMax);
        splitReads[splitReadsLen++] = *frg;
        maxEnd = MAX(maxEnd, end);
        continue;
      }

      //  No thick overlap found.  We need to break right here before the current read.  We used
      //  to try to place contained reads with their container.  For simplicity, we instead just
      //  make a new unitig, letting the main() decide what to do with them (e.g., bubble pop or try
      //  to place all reads in singleton tigs as contained reads again).

      numCreated++;
      makeNewUnitig(tigs, splitReadsLen, splitReads);
      tig = tigs[ti];

      //  Done with the split, save the current read.  This resets everything.

      splitReadsLen = 0;
      splitReads[splitReadsLen++] = *frg;

      maxEnd = end;
    }


    //  If we did any splitting, then the length of the reads in splitReads will be less than the length
    //  of the path in the current unitig.  Make a final new unitig for the remaining reads.
    //
    if (splitReadsLen != tig->ufpath.size()) {
      numCreated++;
      makeNewUnitig(tigs, splitReadsLen, splitReads);

      delete tigs[ti];
      tigs[ti] = NULL;
    }
  }

  delete [] splitReads;

  if (numSplit == 0)
    writeStatus("splitDiscontinuous()-- Tested " F_U32 " tig%s, split none.\n",
                numTested,  (numTested  == 1) ? "" : "s");
  else
    writeStatus("splitDiscontinuous()-- Tested " F_U32 " tig%s, split " F_U32 " tig%s into " F_U32 " new tig%s.\n",
                numTested,  (numTested  == 1) ? "" : "s",
                numSplit,   (numSplit   == 1) ? "" : "s",
                numCreated, (numCreated == 1) ? "" : "s");
}

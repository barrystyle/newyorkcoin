// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/system.h>

unsigned int Digishield(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    int nHeight = pindexLast->nHeight + 1;
    const int64_t retargetTimespan = params.nPowTargetTimespan;
    const int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    int64_t nModulatedTimespan = nActualTimespan;
    int64_t nMaxTimespan;
    int64_t nMinTimespan;

    if (true) /*if (params.fDigishieldDifficultyCalculation)*/ 
    {
        // amplitude filter - thanks to daft27 for this code
        nModulatedTimespan = retargetTimespan + (nModulatedTimespan - retargetTimespan) / 8;

        nMinTimespan = retargetTimespan - (retargetTimespan / 4);
        nMaxTimespan = retargetTimespan + (retargetTimespan / 2);
    } else if (nHeight > 10000) {
        nMinTimespan = retargetTimespan / 4;
        nMaxTimespan = retargetTimespan * 4;
    } else if (nHeight > 5000) {
        nMinTimespan = retargetTimespan / 8;
        nMaxTimespan = retargetTimespan * 4;
    } else {
        nMinTimespan = retargetTimespan / 16;
        nMaxTimespan = retargetTimespan * 4;
    }

    // Limit adjustment step
    if (nModulatedTimespan < nMinTimespan)
        nModulatedTimespan = nMinTimespan;
    else if (nModulatedTimespan > nMaxTimespan)
        nModulatedTimespan = nMaxTimespan;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nModulatedTimespan;
    bnNew /= retargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int KimotoGravityWell(const CBlockIndex* pindexLast, const CBlockHeader* pblock, uint64_t TargetBlocksSpacingSeconds, uint64_t PastBlocksMin, uint64_t PastBlocksMax, const Consensus::Params& params)
{
    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    uint64_t PastBlocksMass = 0;
    int64_t PastRateActualSeconds = 0;
    int64_t PastRateTargetSeconds = 0;
    double PastRateAdjustmentRatio = double(1);
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
    double EventHorizonDeviation;
    double EventHorizonDeviationFast;
    double EventHorizonDeviationSlow;
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    if (BlockLastSolved == NULL ||
        BlockLastSolved->nHeight == 0 ||
        (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) {
        return bnPowLimit.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++)
    {
        if (PastBlocksMax > 0 && i > PastBlocksMax) break;
        PastBlocksMass++;

        if (i == 1) {
            PastDifficultyAverage.SetCompact(BlockReading->nBits);
        } else if (arith_uint256().SetCompact(BlockReading->nBits) >= PastDifficultyAveragePrev) {
            PastDifficultyAverage = ((arith_uint256().SetCompact(BlockReading->nBits) - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev;
        } else {
            PastDifficultyAverage = PastDifficultyAveragePrev - ((PastDifficultyAveragePrev - arith_uint256().SetCompact(BlockReading->nBits)) / i);
        }

        PastDifficultyAveragePrev = PastDifficultyAverage;
        PastRateActualSeconds = BlockLastSolved->GetBlockTime() - BlockReading->GetBlockTime();
        PastRateTargetSeconds = TargetBlocksSpacingSeconds * PastBlocksMass;
        PastRateAdjustmentRatio = double(1);

        if (PastRateActualSeconds < 0) PastRateActualSeconds = 0;

        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0)
            PastRateAdjustmentRatio = double(PastRateTargetSeconds) / double(PastRateActualSeconds);

        EventHorizonDeviation = 1 + (0.7084 * pow((double(PastBlocksMass) / double(144)), -1.228));
        EventHorizonDeviationFast = EventHorizonDeviation;
        EventHorizonDeviationSlow = 1 / EventHorizonDeviation;

        if (PastBlocksMass >= PastBlocksMin) {
            if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast)) {
                assert(BlockReading);
                break;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }

        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        bnNew *= PastRateActualSeconds;
        bnNew /= PastRateTargetSeconds;
    }

    if (bnNew > bnPowLimit) bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, const Consensus::Params& params)
{
    static const int64_t BlocksTargetSpacing = 30;
    unsigned int TimeDaySeconds = 60 * 60 * 24;
    int64_t PastSecondsMin = TimeDaySeconds * 0.01;
    int64_t PastSecondsMax = TimeDaySeconds * 0.14;
    uint64_t PastBlocksMin = PastSecondsMin / BlocksTargetSpacing;
    uint64_t PastBlocksMax = PastSecondsMax / BlocksTargetSpacing;

    return KimotoGravityWell(pindexLast, pblock, BlocksTargetSpacing, PastBlocksMin, PastBlocksMax, params);
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

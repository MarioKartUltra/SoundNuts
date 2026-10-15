// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
#include "soundnuts/registry.hpp"
#include "soundnuts/mod_engine.hpp"
#include <cstdio>
#include <cmath>
using namespace soundnuts;
int main(){
    Registry reg;
    const double sr=48000; const int N=512;
    ModConfig cfg; cfg.controlRateHz=600.0; cfg.rateScale=2.0; cfg.smoothingMs=2.0;
    int failures=0;
    for(int i=0;i<reg.size();++i){
        const auto& sys=reg.system(i);
        ModEngine eng(sys,cfg); eng.prepare(sr,N);
        printf("== %-9s dim=%d ==\n", sys.name().c_str(), sys.dim());
        for(double bif : {0.0,0.25,0.5,0.75,0.95}){
            eng.retrigger();
            ModBlock blk;
            double lo=1e9,hi=-1e9; bool finite=true;
            for(int b=0;b<20;++b){ eng.process(bif,N,blk);
              for(int c=0;c<blk.channels;++c) for(int n=0;n<N;++n){ float v=blk.out[c][n];
                if(!std::isfinite(v)) finite=false; if(v<lo)lo=v; if(v>hi)hi=v; } }
            Regime r=eng.currentRegime(bif);
            const char* rn = r==Regime::FixedPoint?"fixed":r==Regime::LimitCycle?"cycle":r==Regime::Chaos?"chaos":"diverg";
            bool inRange = lo>=-1.0001 && hi<=1.0001;
            printf("  bif=%.2f regime=%-6s out=[% .3f,% .3f] %s%s\n", bif, rn, lo, hi,
                   finite?"finite":"NaN!", inRange?"":" OUT-OF-RANGE!");
            if(!finite||!inRange) ++failures;
        }
    }
    printf("\n%d issue(s)\n", failures);
    return failures;
}

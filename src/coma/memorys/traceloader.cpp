#include "traceloader.h"
using namespace MemSim;

// return the number of processors
UINT traceloader(TraceFile* &pTrf, char* strName)
{
    pTrf = new TraceFile(strName);

    TraceFile::OpenResult nTrfOpenRes = pTrf->open();

    if(nTrfOpenRes != TraceFile::OPEN_RES_OK) {
        cerr << "Error opening trace file. Error Code = "
            << nTrfOpenRes << endl;

        exit(1);
    }

    // get processor number
    return pTrf->getProcCnt();
}

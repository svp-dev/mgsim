#include "tracefile.h"
#include <stdlib.h>

using namespace std;

/*---------------------------------------------------------------------------*/

TraceFile::TraceFile(const TraceFile& trf)
{
    /**
     * Empty (no coppies allowed)
     */
}

void TraceFile::fillBuffer()
{
    m_curEntry++;
    m_curEntryReadProcs = 0;

    for(unsigned int i=0; i < m_procsCnt; i++) {
        m_readCurEntry[i] = false;
    }

    size_t processedBytes = 0;
    size_t readBytes      = fread(m_buffer, 1, m_bufSz, m_fHnd);

    if(readBytes <  sizeof(bool) * m_procsCnt) {
        if(feof(m_fHnd))
            m_bufferFillRes = TraceFile::READ_RES_EOF;
        else 
            m_bufferFillRes = TraceFile::READ_RES_FILE_ERROR;

        return;
    }

    bool* p = static_cast<bool *>(m_buffer);

    for(unsigned int i=0; i < m_procsCnt; i++) {
        if(*p) {
            m_inBufPtrs[i] = reinterpret_cast<Entry *>(++p);
            p = reinterpret_cast<bool *>(m_inBufPtrs[i] + 1);

            processedBytes += sizeof(bool) + sizeof(Entry);
        } else {
            m_inBufPtrs[i] = NULL;
            p++;
            processedBytes += sizeof(bool);
        }

        if(readBytes < processedBytes) {
            m_bufferFillRes = TraceFile::READ_RES_FILE_ERROR;
            return;
        }
    }

    if(fseek(m_fHnd, (long)processedBytes - (long)readBytes, SEEK_CUR) != 0)
        m_bufferFillRes = TraceFile::READ_RES_FILE_ERROR;
    else
        m_bufferFillRes = TraceFile::READ_RES_OK;
}

TraceFile::CloseResult TraceFile::close(bool force)
{
    if(m_fHnd == NULL) {
        return TraceFile::CLOSE_RES_INVALID_CALL;
    }

    if(fclose(m_fHnd) == 0) {
        m_fHnd  = NULL;
        return TraceFile::CLOSE_RES_OK;
    } else {
        if(!force) {
            return TraceFile::CLOSE_RES_FILE_ERROR;
        }
    }

    if(m_buffer != NULL) {
        free(m_buffer);
        m_buffer = NULL;
    }

    if(m_readCurEntry != NULL) {
        delete [] m_readCurEntry;
        m_readCurEntry = NULL;
    }

    if(m_inBufPtrs != NULL) {
        delete [] m_inBufPtrs;
        m_inBufPtrs = NULL;
    }

	return TraceFile::CLOSE_RES_FILE_ERROR;
}

/*---------------------------------------------------------------------------*/

TraceFile::TraceFile(const char* fName)
: m_fName(fName)
{
    m_fHnd         = NULL;
    m_buffer       = NULL;
    m_readCurEntry = NULL;
    m_inBufPtrs    = NULL;
}

TraceFile::~TraceFile()
{
    close(true);
}

/*---------------------------------------------------------------------------*/

TraceFile::OpenResult TraceFile::open()
{
    char fileSign[3];

    if(m_fHnd != NULL) {
        return TraceFile::OPEN_RES_INVALID_CALL;
    }

    /* Open file */
    m_fHnd = fopen(m_fName.c_str(), "rb");

    if(m_fHnd == NULL) {
        return TraceFile::OPEN_RES_FILE_ERROR;
    }

    /* Check file signature */
    if(fread(fileSign, 1, 3, m_fHnd) != 3) {
        close(true);
        return TraceFile::OPEN_RES_FILE_ERROR;
    } else {
        if(fileSign[0] != 'T' || fileSign[1] != 'R' || fileSign[2] != 'F') {
            close(true);
            return TraceFile::OPEN_RES_INVALID_FILE_FORMAT;
        }
    }

    /* Read number of processors */
    if(fread(&m_procsCnt, sizeof(unsigned int), 1, m_fHnd) != 1) {
        close(true);
        return TraceFile::OPEN_RES_FILE_ERROR;
    }

    /* Allocate buffers */

    m_bufSz  = m_procsCnt * (sizeof(bool) + sizeof(TraceFile::Entry));
    m_buffer = malloc(m_bufSz);

    if(m_buffer == NULL) {
        close(true);
        return TraceFile::OPEN_RES_MALLOC_ERROR;
    }

    m_readCurEntry = new bool[m_procsCnt];

    if(m_readCurEntry == NULL) {
        close(true);
        return TraceFile::OPEN_RES_MALLOC_ERROR;
    }

    m_inBufPtrs = new Entry*[m_procsCnt];

    if(m_inBufPtrs == NULL) {
        close(true);
        return TraceFile::OPEN_RES_MALLOC_ERROR;
    }

    fillBuffer();

    if(m_bufferFillRes != TraceFile::READ_RES_OK) {
        close(true);
        return TraceFile::OPEN_RES_FILE_ERROR;
    }

    m_curEntry = 0;

    return TraceFile::OPEN_RES_OK;
}

TraceFile::CloseResult TraceFile::close()
{
   return close(false);
}

TraceFile::ReadResult TraceFile::readNext(unsigned int pid, Entry& e)
{
    TraceFile::ReadResult ret = TraceFile::READ_RES_NULL_ENTRY;

    if(m_fHnd == NULL) {
        return TraceFile::READ_RES_INVALID_CALL;
    }

    if(pid >= m_procsCnt) {
        return TraceFile::READ_RES_INVALID_PID;
    }

    if(m_bufferFillRes != TraceFile::READ_RES_OK) {
        return m_bufferFillRes;
    }

    if(m_readCurEntry[pid]) {
        return TraceFile::READ_RES_INVALID_PID;
    }

    m_readCurEntry[pid] = true;

    if(m_inBufPtrs[pid] != NULL) {
        e.addr = m_inBufPtrs[pid]->addr;
        e.type = m_inBufPtrs[pid]->type;

        ret = TraceFile::READ_RES_OK;
    }

    m_curEntryReadProcs++;

    if(m_curEntryReadProcs == m_procsCnt) {
        fillBuffer();
    }

    return ret;
}

/*---------------------------------------------------------------------------*/

bool TraceFile::eof() const
{
    if(m_fHnd == NULL)
        return true;
    else
        return m_bufferFillRes == TraceFile::READ_RES_EOF;
}

/*---------------------------------------------------------------------------*/

const std::string& TraceFile::getFileName() const
{
    return m_fName;
}

unsigned int TraceFile::getProcCnt() const
{
    if(m_fHnd != NULL) 
        return m_procsCnt;
    else
        return 0;
}

unsigned int TraceFile::getCurEntry(unsigned int pid) const
{
    if(m_fHnd != NULL)
        return m_curEntry;
    else
        return -1;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/


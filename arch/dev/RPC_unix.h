// -*- c++ -*-
#ifndef RPC_UNIX_H
#define RPC_UNIX_H

#include "RPC.h"
#include <sim/inspect.h>

#include <vector>
#include <dirent.h>

namespace Simulator
{
    class UnixInterface : public Object, public IRPCServiceProvider, public Inspect::Interface<Inspect::Info>
    {
        typedef int       HostFD;
        typedef size_t    VirtualFD;

        struct VirtualDescriptor
        {
            bool        active;
            HostFD      hfd;
            DIR         *dir;
            CycleNo     cycle_open;
            CycleNo     cycle_use;
            std::string fname;

            VirtualDescriptor() : active(false), hfd(-1), dir(NULL), cycle_open(0), cycle_use(0), fname() {}
            VirtualDescriptor(const VirtualDescriptor&) = default;
            VirtualDescriptor& operator=(const VirtualDescriptor&) = default;
        };

        std::vector<VirtualDescriptor> m_vfds;

        VirtualDescriptor* GetEntry(VirtualFD vfd);
        VirtualFD GetNewVFD(HostFD new_hfd);
        VirtualFD DuplicateVFD(VirtualFD original, HostFD new_hfd);
        VirtualDescriptor* DuplicateVFD2(VirtualFD original, VirtualFD target);

        // statistics
        uint64_t m_nrequests;
        uint64_t m_nfailures;
        uint64_t m_nstats;
        uint64_t m_nreads;
        uint64_t m_nread_bytes;
        uint64_t m_nwrites;
        uint64_t m_nwrite_bytes;

    public:

        UnixInterface(const std::string& name, Object& parent);

        void Service(uint32_t procedure_id,
                     std::vector<char>& res1, size_t res1_maxsize,
                     std::vector<char>& res2, size_t res2_maxsize,
                     const std::vector<char>& arg1,
                     const std::vector<char>& arg2,
                     uint32_t arg3, uint32_t arg4);

        const std::string& GetName() const override;

        void Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const;

    };


}


#endif

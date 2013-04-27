#ifndef RUSAGE_H
#define RUSAGE_H

class ResourceUsage
{
public:
    typedef long long usecs_t;
    typedef long      kbytes_t;

    ResourceUsage(bool sample = false, bool set_initial = false);
    usecs_t       GetUserTime() const { return m_utime; }
    usecs_t       GetSystemTime() const { return m_stime; }
    kbytes_t      GetMaxResidentSize() const { return m_maxrss; }

    ResourceUsage operator-(const ResourceUsage& other) const;

private:
    static const   ResourceUsage* initial;
    long long      m_utime; // microseconds
    long long      m_stime; // microseconds
    long           m_maxrss; // kilobytes
};

#endif

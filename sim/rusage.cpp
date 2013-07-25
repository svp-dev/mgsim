#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <sys_config.h>
#include "rusage.h"

ResourceUsage::ResourceUsage(bool sample, bool set_initial)
    : m_utime(0), m_stime(0), m_maxrss(0)
{
    if (set_initial)
    {
	ResourceUsage::initial = this;
    }

    if (sample)
    {
#ifdef HAVE_GETRUSAGE
	rusage res;

	if (getrusage(RUSAGE_SELF, &res))
	    perror("getrusage");

	m_utime = res.ru_utime.tv_sec * 1000000ll + res.ru_utime.tv_usec;
	m_stime = res.ru_stime.tv_sec * 1000000ll + res.ru_stime.tv_usec;
#if defined(HAVE_STRUCT_RUSAGE_RU_MAXRSS) && !defined(RUSAGE_MAX_RSS_EMPTY)
	m_maxrss = res.ru_maxrss / RUSAGE_MAX_RSS_DIVIDER;
#elif defined(HAVE_PROC_STATM)
	int fd;
	char buf[64] = {0};
	if ((fd = open("/proc/self/statm", O_RDONLY)) != -1)
	{
            if (read(fd, buf, 63) != -1)
            {
		long v = 0;
		sscanf(buf, "%*d %ld", &v);
		m_maxrss = v;
            }
            close(fd);
	}
#endif

	if (!set_initial)
	{
	    m_utime -= initial->m_utime;
	    m_stime -= initial->m_stime;
	    m_maxrss -= initial->m_maxrss;
	}
#endif
    }
}

static ResourceUsage _initial(true, true);
const ResourceUsage* ResourceUsage::initial = 0;

ResourceUsage
ResourceUsage::operator-(const ResourceUsage& other) const
{
    ResourceUsage res;
    res.m_utime = m_utime - other.m_utime;
    res.m_stime = m_stime - other.m_stime;
    res.m_maxrss = m_maxrss - other.m_maxrss;
    return res;
}

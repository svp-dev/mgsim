#include <sys_config.h>
#include <algorithm> // sort
#include <fnmatch.h> // fnmatch
#include <ctime>     // time, gmtime, asctime
#include <unistd.h>  // gethostname
#include <sim/binarysampler.h>
#include <sim/sampling.h>
#include <sim/config.h>

using namespace std;
namespace Simulator
{
    BinarySampler::BinarySampler(const VariableRegistry& registry)
        : m_datasize(0), m_vars(), m_registry(registry)
    {}

    void BinarySampler::SelectVariables(ostream& os, const Config& config,
                                        const vector<string>& pats)
    {
        typedef pair<const string*,
                     const VariableRegistry::VarInfo*> varsel_t;
        typedef vector<varsel_t> varvec_t;
        varvec_t vars;

        //
        // Select variables to sample
        //
        for (auto& i : pats)
            for (auto& j : m_registry.GetRegistry())
            {
                if (FNM_NOMATCH == fnmatch(i.c_str(), j.first.c_str(), 0))
                    continue;

                if (j.second.type == Serialization::SV_OTHER)
                    throw exceptf<>("Cannot monitor the non-scalar variable %s (selected by %s)", j.first.c_str(), i.c_str());

                vars.push_back(make_pair(&j.first, &j.second));
            }

        if (vars.size() >= 2)
            // Sort: to increase cache locality in SampleVariables.
            //
            // We sort everything but the first and last variables,
            // which should be the cycle counters. The cycle counters
            // must be sampled once before and after everything else,
            // to evaluate how imprecise the measurement is.
            sort(vars.begin()+1, vars.end()-1,
                 [](const varsel_t& left, const varsel_t& right) -> bool
                 { return left.second->var < right.second->var; });

        //
        // Generate header for output file
        //
        time_t cl = time(0);
        string timestr = asctime(gmtime(&cl));

        os << "# date: " << timestr // asctime already embeds a newline character
           << "# generator: " << PACKAGE_STRING << endl;

        char hn[255];
        if (gethostname(hn, 255) == 0)
            os << "# host: " << hn << endl;

        vector<pair<string, string> > rawconf = config.getRawConfiguration();
        os << "# configuration: " << rawconf.size() << endl;
        for (auto& i : rawconf)
            os << i.first << " = " << i.second << endl;

        m_datasize = 0;
        m_vars.clear();
        os << "# varinfo: " << vars.size() << endl;
        for (auto& i : vars)
        {
            m_datasize += i.second->width;
            m_vars.push_back(make_pair((const char*)i.second->var, i.second->width));
            m_registry.ListVariables_onevar(os, *i.first, *i.second);
        }
        os << "# recwidth: " << m_datasize << endl;
    }

}

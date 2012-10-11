#include "simreadline.h"
#include <arch/dev/Display.h>

#include <sys/time.h>
#include <sstream>
#include <string>
#include <cstdlib>

#if defined(HAVE_LIBREADLINE) && !defined(HAVE_READLINE_READLINE_H)
extern "C" {
char *cmdline = NULL;
}
#endif

using namespace Simulator;
using namespace std;

int CommandLineReader::ReadLineHook(void) {
    Display *display = Display::GetDisplay();
    if (!display) 
        return 0;

    // readline is annoying: the documentation says the event
    // hook is called no more than 10 times per second, but
    // tests show it _can_ be much more often than this. But
    // we don't want to refresh the display or check events
    // too often (it's expensive...) So we keep track of time
    // here as well, and force 10ths of seconds ourselves.
        
    static long last_check = 0;
        
    struct timeval tv;
    gettimeofday(&tv, 0);
    long current = tv.tv_sec * 10 + tv.tv_usec / 100000;
    if (current - last_check)
    {
        display->CheckEvents();
        last_check = current;
    }
    return 0;
}

CommandLineReader::CommandLineReader()
    : m_histfilename() {
#ifdef HAVE_LIBREADLINE
    rl_event_hook = &ReadLineHook;
# ifdef HAVE_READLINE_HISTORY
    ostringstream os;
    os << getenv("HOME") << "/.mgsim_history";
    m_histfilename = os.str();
    read_history(m_histfilename.c_str());
# endif
#endif
}

CommandLineReader::~CommandLineReader() {
#ifdef HAVE_LIBREADLINE
    rl_event_hook = 0;
#endif
    CheckPointHistory();
}

char* CommandLineReader::GetCommandLine(const string& prompt)
{
#ifdef HAVE_LIBREADLINE
    char* str = readline(prompt.c_str());
# ifdef HAVE_READLINE_HISTORY
    if (str != NULL && *str != '\0')
    {
        add_history(str);
    }
# endif
#else // !HAVE_LIBREADLINE
    std::string line;

    std::cout << prompt;
    std::cout.flush();

    std::getline(std::cin, line);
    if (std::cin.fail())
        return 0;
    
    char *str = (char*)malloc(line.size() + 1);
    assert(str != 0);
    strcpy(str, line.c_str());
#endif
    return str;
}

void CommandLineReader::CheckPointHistory() {
#if defined(HAVE_LIBREADLINE) && defined(HAVE_READLINE_HISTORY)
    write_history(m_histfilename.c_str());
#endif
}


vector<string> Tokenize(const string& str, const string& sep)
{
    vector<string> tokens;
    for (size_t next, pos = str.find_first_not_of(sep); pos != string::npos; pos = next)
    {
        next = str.find_first_of(sep, pos);
        if (next == string::npos)
        {
            tokens.push_back(str.substr(pos));  
        }
        else
        {
            tokens.push_back(str.substr(pos, next - pos));
            next = str.find_first_not_of(sep, next);
        }
    }
    return tokens;
}


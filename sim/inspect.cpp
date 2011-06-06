#include "inspect.h"

namespace Inspect
{

void
ListCommands::ListSupportedCommands(std::ostream& out) const
{
    out << (dynamic_cast<const Interface_<Info>*>(this)  ? "i " : "  ")
        << (dynamic_cast<const Interface_<Read>*>(this)  ? "p " : "  ")
        << (dynamic_cast<const Interface_<Line>*>(this)  ? "l " : "  ")
        << (dynamic_cast<const Interface_<Trace>*>(this) ? "t " : "  ");
}

}


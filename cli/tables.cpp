#include "commands.h"

const
command_descriptor command_table[] =
{
    { { "aliases", 0 },               0, 0,  cmd_aliases,    "aliases",           "List all command aliases." },
    { { "breakpoint", 0  },           0, 0,  cmd_bp_list,    "breakpoint",        "List all current breakpoints." },
    { { "breakpoint", "add", 0 },     2, 2,  cmd_bp_add,     "breakpoint add MODE ADDR", "Set a breakpoint at address ADDR with MODE." },
    { { "breakpoint", "clear", 0 },   0, 0,  cmd_bp_clear,   "breakpoint clear",  "Clear all breakpoints." },
    { { "breakpoint", "del", 0 },     1, 1,  cmd_bp_del,     "breakpoint del ID", "Delete the breakpoint specified by ID." },
    { { "breakpoint", "disable", 0 }, 1, 1,  cmd_bp_disable, "breakpoint disable ID", "Disable the breakpoint specified by ID." },
    { { "breakpoint", "enable", 0 },  1, 1,  cmd_bp_enable,  "breakpoint enable ID", "Enable the breakpoint specified by ID." },
    { { "breakpoint", "off", 0  },    0, 0,  cmd_bp_off,     "breakpoint off",    "Disable breakpoint detection." },
    { { "breakpoint", "on", 0  },     0, 0,  cmd_bp_on,      "breakpoint on",     "Enable breakpoint detection." },
    { { "breakpoint", "state", 0  },  0, 0,  cmd_bp_state,   "breakpoint state",  "Report which breakpoints have been reached." },
    { { "disassemble", 0 },           1, 2,  cmd_disas,      "disassemble ADDR [SZ]", "Disassemble the program from address ADDR." },
    { { "help", 0 },                  0, 1,  cmd_help,       "help [COMMAND]",    "Print the help text for COMMAND, or this text if no command is specified." },
    { { "info", 0 },                  1, -1, cmd_info,       "info COMPONENT [ARGS...]",    "Show help/configuration/layout for COMPONENT." },
    { { "line", 0 },                  2, 2,  cmd_line,       "line COMPONENT ADDR", "Lookup the memory line at address ADDR in the memory system COMPONENT." },
    { { "lookup", 0 },                1, 1,  cmd_lookup,     "lookup ADDR",       "Look up the program symbol closest to address ADDR." },
    { { "quit", 0 },                  0, 0,  cmd_quit,       "quit",              "Exit the simulation." },
    { { "inspect", 0 },               1, -1, cmd_inspect,    "inspect NAME [ARGS...]", "Inspect NAME. See 'info NAME' for details." },
    { { "run", 0 },                   0, 0,  cmd_run,        "run",               "Run the system until it is idle or deadlocks. Livelocks will not be reported." },
    { { "show", "vars", 0 },          0, 1,  cmd_show_vars,  "show vars [PAT]",   "List monitoring variables matching PAT." },
    { { "show", "syms", 0 },          0, 1,  cmd_show_syms,  "show syms [PAT]",   "List program symbols matching PAT." },
    { { "show", "components", 0 },    0, 2,  cmd_show_components, "show components [PAT] [LEVEL]",   "List components matching PAT (at most LEVELs)." },
    { { "show", "processes", 0 },     0, 1,  cmd_show_processes, "show processes [PAT]",   "List processes matching PAT." },
    { { "show", "devicedb", 0 },      0, 0,  cmd_show_devdb, "show devicedb",     "List the I/O device identifier database." },
    { { "state", 0 },                 0, 0,  cmd_state,       "state",            "Show the state of the system. Idle components are left out." },
    { { "statistics", 0 },            0, 0,  cmd_stats,       "statistics",       "Print the current simulation statistics." },
    { { "step", 0 },                  0, 1,  cmd_run,         "step [N]",         "Advance the system by N clock cycles (default 1)." },
    { { "trace", "line", 0 },         2, 3,  cmd_trace_line,   "trace line COMPONENT ADDR [clear]",  "Enable/Disable tracing of the cache line at address ADDR by memory COMPONENT." },
    { { "trace", 0 },                 0, -1, cmd_trace_debug,  "trace [FLAGS...]", "Show current traces / toggle tracing of FLAGS." },

    { { 0 }, 0, 0, 0, 0, 0 }
};

const
command_alias alias_table[] =
{
    { "b"       , { "breakpoint", "add", "x", 0 } },
    { "bp"      , { "breakpoint", 0 } },
    { "break"   , { "breakpoint", "add", "x", 0 } },
    { "bye"     , { "quit", 0 } },
    { "c"       , { "run",  0 } },
    { "cont"    , { "run",  0 } },
    { "continue", { "run",  0 } },
    { "d"       , { "disassemble", 0 } },
    { "debug"   , { "trace", 0 } },
    { "dis"     , { "disassemble", 0 } },
    { "exit"    , { "quit", 0 } },
    { "h"       , { "help", 0 } },
    { "i"       , { "info", 0 } },
    { "p"       , { "inspect", 0 } },
    { "print"   , { "inspect", 0 } },
    { "q"       , { "quit", 0 } },
    { "r"       , { "run",  0 } },
    { "read"    , { "inspect", 0 } },
    { "s"       , { "step", 0 } },
    { "stats"   , { "statistics", 0 } },
    { "t"       , { "trace", 0 } },

    { 0, { 0 } },
};

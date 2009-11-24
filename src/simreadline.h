#ifndef SIMREADLINE_H
#define SIMREADLINE_H

#include <cstdio>

#ifdef HAVE_LIBREADLINE
#if defined(HAVE_READLINE_READLINE_H)
#include <readline/readline.h>
#elif defined(HAVE_READLINE_H)
#include <readline.h>
#else /* !defined(HAVE_READLINE_H) */
extern "C" char *readline ();
#endif /* !defined(HAVE_READLINE_H) */
extern "C" {
char *cmdline = NULL;
extern int (*rl_event_hook)(void);
}
#else /* !defined(HAVE_READLINE_READLINE_H) */
/* no readline */
#endif /* HAVE_LIBREADLINE */

#ifdef HAVE_READLINE_HISTORY
#if defined(HAVE_READLINE_HISTORY_H)
#include <readline/history.h>
#elif defined(HAVE_HISTORY_H)
#include <history.h>
#else /* !defined(HAVE_HISTORY_H) */
extern "C" {
extern void add_history (const char*);
extern int write_history (const char*);
extern int read_history (const char*);
}
#endif /* defined(HAVE_READLINE_HISTORY_H) */
/* no history */
#endif /* HAVE_READLINE_HISTORY */


#endif

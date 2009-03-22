
AC_DEFUN([AC_LIB_READLINE], [
  AC_CACHE_CHECK([for a readline compatible library],
                 ac_cv_lib_readline, [
    ORIG_LIBS=$LIBS
    for readline_lib in readline edit editline; do
      for termcap_lib in "" ncurses curses termcap; do
        if test -z "$termcap_lib"; then
          TRY_LIB="-l$readline_lib"
        else
          TRY_LIB="-l$readline_lib -l$termcap_lib"
        fi
        LIBS="$ORIG_LIBS $TRY_LIB"
        AC_TRY_LINK_FUNC(readline, ac_cv_lib_readline="$TRY_LIB")
        if test -n "$ac_cv_lib_readline"; then
          break
        fi
      done
      if test -n "$ac_cv_lib_readline"; then
        break
      fi
    done
    if test -z "$ac_cv_lib_readline"; then
      ac_cv_lib_readline="no"
      LIBS=$ORIG_LIBS
    fi
  ])

  if test "$ac_cv_lib_readline" != "no"; then
    AC_DEFINE(HAVE_LIBREADLINE, 1,
              [Define if you have a readline compatible library])
    AC_CHECK_HEADERS(readline.h readline/readline.h)
    AC_CACHE_CHECK([whether readline supports history],
                   ac_cv_lib_readline_history, [
      ac_cv_lib_readline_history="no"
      AC_TRY_LINK_FUNC(add_history, ac_cv_lib_readline_history="yes")
    ])
    if test "$ac_cv_lib_readline_history" = "yes"; then
      AC_DEFINE(HAVE_READLINE_HISTORY, 1,
                [Define if your readline library has \`add_history'])
      AC_CHECK_HEADERS(history.h readline/history.h)
    fi
  fi
])

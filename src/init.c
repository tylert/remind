/***************************************************************/
/*                                                             */
/*  INIT.C                                                     */
/*                                                             */
/*  Initialize remind; perform certain tasks between           */
/*  iterations in calendar mode; do certain checks after end   */
/*  in normal mode.                                            */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2022 by Dianne Skoll                    */
/*                                                             */
/***************************************************************/

#include "version.h"
#include "config.h"

#define L_IN_INIT 1
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <pwd.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef HAVE_INITGROUPS
#include <grp.h>
#endif

#include "types.h"
#include "globals.h"
#include "protos.h"
#include "expr.h"
#include "err.h"

/***************************************************************
 *
 *  Command line options recognized:
 *
 *  -n       = Output next trigger date of each reminder in
 *             simple calendar format.
 *  -r       = Disallow RUN mode
 *  -c[n]    = Produce a calendar for n months (default = 1)
 *  -@[n,m,b]= Colorize n=0 VT100 n=1 85 n=2 True m=0 dark terminal m=1 light
 *             b=0 ignore SHADE b=1 respect SHADE
 *  -w[n,n,n] = Specify output device width, padding and spacing
 *  -s[n]    = Produce calendar in "simple calendar" format
 *  -p[n]    = Produce calendar in format compatible with rem2ps
 *  -l       = Prefix simple calendar lines with a comment containing
 *             their trigger line numbers and filenames
 *  -v       = Verbose mode
 *  -o       = Ignore ONCE directives
 *  -a       = Don't issue timed reminders which will be queued
 *  -q       = Don't queue timed reminders
 *  -t       = Trigger all reminders (infinite delta)
 *  -h       = Hush mode
 *  -f       = Do not fork
 *  -dchars  = Debugging mode:  Chars are:
 *             f = Trace file openings
 *             e = Echo input lines
 *             x = Display expression evaluation
 *             t = Display trigger dates
 *             v = Dump variables at end
 *             l = Display entire line in error messages
 *  -e       = Send messages normally sent to stderr to stdout instead
 *  -z[n]    = Daemon mode waking up every n (def 1) minutes.
 *  -bn      = Time format for cal (0, 1, or 2)
 *  -xn      = Max. number of iterations for SATISFY
 *  -uname   = Run as user 'name' - only valid when run by root.  If run
 *             by non-root, changes environment but not effective uid.
 *  -kcmd    = Run 'cmd' for MSG-type reminders instead of printing to stdout
 *  -iVAR=EXPR = Initialize and preserve VAR.
 *  -m       = Start calendar with Monday instead of Sunday.
 *  -j[n]    = Purge all junk from reminder files (n = INCLUDE depth)
 *  A minus sign alone indicates to take input from stdin
 *
 **************************************************************/

/* For parsing an integer */
#define PARSENUM(var, s)   \
var = 0;                   \
while (isdigit(*(s))) {    \
    var *= 10;             \
    var += *(s) - '0';     \
    s++;                   \
}

static void ChgUser(char const *u);
static void InitializeVar(char const *str);

static char const *BadDate = "Illegal date on command line\n";
static void AddTrustedUser(char const *username);

static DynamicBuffer default_filename_buf;

/***************************************************************/
/*                                                             */
/*  DefaultFilename                                            */
/*                                                             */
/*  If we're invoked as "rem" rather than "remind", use a      */
/*  default filename.  Use $DOTREMINDERS or $HOME/.reminders   */
/*                                                             */
/***************************************************************/
static char const *DefaultFilename(void)
{
    char const *s;

    DBufInit(&default_filename_buf);

    s = getenv("DOTREMINDERS");
    if (s) {
	return s;
    }

    s = getenv("HOME");
    if (!s) {
	fprintf(stderr, "HOME environment variable not set.  Unable to determine reminder file.\n");
	exit(EXIT_FAILURE);
    }
    DBufPuts(&default_filename_buf, s);
    DBufPuts(&default_filename_buf, "/.reminders");
    return DBufValue(&default_filename_buf);
}

/***************************************************************/
/*                                                             */
/*  InitRemind                                                 */
/*                                                             */
/*  Initialize the system - called only once at beginning!     */
/*                                                             */
/***************************************************************/
void InitRemind(int argc, char const *argv[])
{
    char const *arg;
    int i;
    int y, m, d, rep;
    Token tok;
    int InvokedAsRem = 0;
    char const *s;
    int weeks;
    int x;
    int jul;
    int ttyfd;
    struct winsize w;

    jul = NO_DATE;

    /* If stdout is a terminal, initialize $FormWidth to terminal width-8,
       but clamp to [20, 500] */
    if (isatty(STDOUT_FILENO)) {
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
	    FormWidth = w.ws_col - 8;
	    if (FormWidth < 20) FormWidth = 20;
	    if (FormWidth > 500) FormWidth = 500;
	}
    }

    /* Initialize global dynamic buffers */
    DBufInit(&Banner);
    DBufInit(&LineBuffer);
    DBufInit(&ExprBuf);

    DBufPuts(&Banner, L_BANNER);

    PurgeFP = NULL;

    /* Make sure remind is not installed set-uid or set-gid */
    if (getgid() != getegid() ||
	getuid() != geteuid()) {
	fprintf(ErrFp, "\nRemind should not be installed set-uid or set-gid.\nCHECK YOUR SYSTEM SECURITY.\n");
	exit(EXIT_FAILURE);
    }

    y = NO_YR;
    m = NO_MON;
    d = NO_DAY;
    rep = NO_REP;

    RealToday = SystemDate(&CurYear, &CurMon, &CurDay);
    if (RealToday < 0) {
	fprintf(ErrFp, ErrMsg[M_BAD_SYS_DATE], BASE);
	exit(EXIT_FAILURE);
    }
    JulianToday = RealToday;
    FromJulian(JulianToday, &CurYear, &CurMon, &CurDay);

    /* Initialize Latitude and Longitude */
    set_components_from_lat_and_long();

    /* See if we were invoked as "rem" rather than "remind" */
    if (argv[0]) {
	s = strrchr(argv[0], '/');
	if (!s) {
	    s = argv[0];
	} else {
	    s++;
	}
	if (!strcmp(s, "rem")) {
	    InvokedAsRem = 1;
	}
    } else {
        fprintf(stderr, "Invoked with a NULL argv[0]; bailing because that's just plain bizarre.\n");
        exit(1);
    }

    /* Parse the command-line options */
    i = 1;

    while (i < argc) {
	arg = argv[i];
	if (*arg != '-') break; /* Exit the loop if it's not an option */
	i++;
	arg++;
	if (!*arg) {
	    UseStdin = 1;
	    IgnoreOnce = 1;
	    i--;
	    break;
	}
	while (*arg) {
	    switch(*arg++) {
            case '+':
                AddTrustedUser(arg);
                while(*arg) arg++;
                break;

	    case '@':
		UseVTColors = 1;
		if (*arg) {
		    PARSENUM(x, arg);
		    if (x == 1) {
			Use256Colors = 1;
		    } else if (x == 2) {
			UseTrueColors = 1;
		    } else if (x != 0) {
                        fprintf(ErrFp, "%s: -@n,m,b: n must be 0, 1 or 2 (assuming 0)\n",
                                argv[0]);
                    }
		}
		if (*arg == ',') {
		    arg++;
                    if (*arg != ',') {
                        PARSENUM(x, arg);
                        if (x == 0) {
                            TerminalBackground = TERMINAL_BACKGROUND_DARK;
                        } else if (x == 1) {
                            TerminalBackground = TERMINAL_BACKGROUND_LIGHT;
                        } else if (x == 2) {
                            TerminalBackground = TERMINAL_BACKGROUND_UNKNOWN;
                        } else {
                            fprintf(ErrFp, "%s: -@n,m,b: m must be 0, 1 or 2 (assuming 2)\n",
                                    argv[0]);
                        }
                    }
		}
                if (*arg == ',') {
		    arg++;
                    PARSENUM(x, arg);
                    if (x != 0 && x != 1) {
                        fprintf(ErrFp, "%s: -@n,m,b: b must be 0 or 1 (assuming 0)\n",
                                argv[0]);
                        x = 0;
                    }
                    UseBGVTChars = x;
                }
		break;

	    case 'j':
	    case 'J':
		PurgeMode = 1;
	        if (*arg) {
		    PARSENUM(PurgeIncludeDepth, arg);
                }
	        break;
            case 'i':
	    case 'I':
		InitializeVar(arg);
		while(*arg) arg++;
		break;

	    case 'n':
	    case 'N':
		NextMode = 1;
		DontQueue = 1;
		Daemon = 0;
		break;

	    case 'r':
	    case 'R':
		RunDisabled = RUN_CMDLINE;
		break;

	    case 'm':
	    case 'M':
		MondayFirst = 1;
		break;

	    case 'o':
	    case 'O':
		IgnoreOnce = 1;
		break;

	    case 'y':
	    case 'Y':
		SynthesizeTags = 1;
		break;

	    case 't':
	    case 'T':
                if (*arg == 'T' || *arg == 't') {
                    arg++;
                    if (!*arg) {
                        DefaultTDelta = 5;
                    } else {
                        PARSENUM(DefaultTDelta, arg);
                        if (DefaultTDelta < 0) {
                            DefaultTDelta = 0;
                        } else if (DefaultTDelta > 1440) {
                            DefaultTDelta = 1440;
                        }
                    }
                } else if (!*arg) {
		    InfiniteDelta = 1;
		} else {
		    PARSENUM(DeltaOffset, arg);
		    if (DeltaOffset < 0) {
			DeltaOffset = 0;
		    }
		}
		break;
	    case 'e':
	    case 'E':
		ErrFp = stdout;
		break;

	    case 'h':
	    case 'H':
		Hush = 1;
		break;

	    case 'g':
	    case 'G':
		SortByDate = SORT_ASCEND;
		SortByTime = SORT_ASCEND;
		SortByPrio = SORT_ASCEND;
		UntimedBeforeTimed = 0;
		if (*arg) {
		    if (*arg == 'D' || *arg == 'd')
			SortByDate = SORT_DESCEND;
		    arg++;
		}
		if (*arg) {
		    if (*arg == 'D' || *arg == 'd')
			SortByTime = SORT_DESCEND;
		    arg++;
		}
		if (*arg) {
		    if (*arg == 'D' || *arg == 'd')
			SortByPrio = SORT_DESCEND;
		    arg++;
		}
		if (*arg) {
		    if (*arg == 'D' || *arg == 'd')
			UntimedBeforeTimed = 1;
		    arg++;
		}
		break;

	    case 'u':
	    case 'U':
                if (*arg == '+') {
                    ChgUser(arg+1);
                } else {
                    RunDisabled = RUN_CMDLINE;
                    ChgUser(arg);
                }
		while (*arg) arg++;
		break;
	    case 'z':
	    case 'Z':
		DontFork = 1;
		if (*arg == '0') {
		    PARSENUM(Daemon, arg);
		    if (Daemon == 0) Daemon = -1;
		    else if (Daemon < 1) Daemon = 1;
		    else if (Daemon > 60) Daemon = 60;
		} else {
		    PARSENUM(Daemon, arg);
		    if (Daemon<1) Daemon=1;
		    else if (Daemon>60) Daemon=60;
		}
		break;

	    case 'a':
	    case 'A':
		DontIssueAts++;
		break;

	    case 'q':
	    case 'Q':
		DontQueue = 1;
		break;

	    case 'f':
	    case 'F':
		DontFork = 1;
		break;
	    case 'c':
	    case 'C':
		DoCalendar = 1;
		weeks = 0;
		/* Parse the flags */
		while(*arg) {
		    if (*arg == 'a' ||
		        *arg == 'A') {
		        DoSimpleCalDelta = 1;
			arg++;
			continue;
		    }
		    if (*arg == '+') {
		        weeks = 1;
			arg++;
			continue;
		    }
		    if (*arg == 'l' || *arg == 'L') {
		        UseVTChars = 1;
			arg++;
			continue;
		    }
		    if (*arg == 'u' || *arg == 'U') {
			UseUTF8Chars = 1;
			arg++;
			continue;
		    }
		    if (*arg == 'c' || *arg == 'C') {
		        UseVTColors = 1;
		        arg++;
		        continue;
		    }
		    break;
		}
		if (weeks) {
		    PARSENUM(CalWeeks, arg);
		    if (!CalWeeks) CalWeeks = 1;
		} else {
		    PARSENUM(CalMonths, arg);
		    if (!CalMonths) CalMonths = 1;
		}
		break;

	    case 's':
	    case 'S':
		DoSimpleCalendar = 1;
		weeks = 0;
		while(*arg) {
		    if (*arg == 'a' || *arg == 'A') {
			DoSimpleCalDelta = 1;
			arg++;
			continue;
		    }
		    if (*arg == '+') {
		        arg++;
			weeks = 1;
			continue;
		    }
		    break;
		}
		if (weeks) {
		    PARSENUM(CalWeeks, arg);
		    if (!CalWeeks) CalWeeks = 1;
		} else {
		    PARSENUM(CalMonths, arg);
		    if (!CalMonths) CalMonths = 1;
		}
		break;

	    case 'p':
	    case 'P':
		DoSimpleCalendar = 1;
		PsCal = PSCAL_LEVEL1;
		while (*arg == 'a' || *arg == 'A' ||
                       *arg == 'q' || *arg == 'Q' ||
		       *arg == 'p' || *arg == 'P') {
		    if (*arg == 'a' || *arg == 'A') {
			DoSimpleCalDelta = 1;
		    } else if (*arg == 'p' || *arg == 'P') {
			/* JSON interchange formats always include
			   file and line number info */
			DoPrefixLineNo = 1;
			if (PsCal == PSCAL_LEVEL1) {
			    PsCal = PSCAL_LEVEL2;
			} else {
			    PsCal = PSCAL_LEVEL3;
			}
		    } else if (*arg == 'q' || *arg == 'Q') {
                        DontSuppressQuoteMarkers = 1;
                    }
		    arg++;
		}
		PARSENUM(CalMonths, arg);
		if (!CalMonths) CalMonths = 1;
		break;

	    case 'l':
	    case 'L':
		DoPrefixLineNo = 1;
		break;

	    case 'w':
	    case 'W':
		if (*arg != ',') {
                    if (*arg == 't') {
                        arg++;
                        /* -wt means get width from /dev/tty */
                        ttyfd = open("/dev/tty", O_RDONLY);
                        if (!ttyfd) {
                            fprintf(stderr, "%s: `-wt': Cannot open /dev/tty: %s\n",
                                    argv[0], strerror(errno));
                        } else {
                            if (ioctl(ttyfd, TIOCGWINSZ, &w) == 0) {
                                CalWidth = w.ws_col;
                            }
                            close(ttyfd);
                        }
                    } else {
                        PARSENUM(CalWidth, arg);
                        if (CalWidth != 0 && CalWidth < 71) CalWidth = 71;
                        if (CalWidth == 0) {
                            CalWidth = -1;
                        }
                    }
		}
		if (*arg == ',') {
		    arg++;
		    if (*arg != ',') {
			PARSENUM(CalLines, arg);
			if (CalLines > 20) CalLines = 20;
		    }
		    if (*arg == ',') {
			arg++;
			PARSENUM(CalPad, arg);
			if (CalPad > 20) CalPad = 20;
		    }
		}
		break;

	    case 'd':
	    case 'D':
		while (*arg) {
		    switch(*arg++) {
		    case 'e': case 'E': DebugFlag |= DB_ECHO_LINE;   break;
		    case 'x': case 'X': DebugFlag |= DB_PRTEXPR;     break;
		    case 't': case 'T': DebugFlag |= DB_PRTTRIG;     break;
		    case 'v': case 'V': DebugFlag |= DB_DUMP_VARS;   break;
		    case 'l': case 'L': DebugFlag |= DB_PRTLINE;     break;
		    case 'f': case 'F': DebugFlag |= DB_TRACE_FILES; break;
		    default:
		        fprintf(ErrFp, ErrMsg[M_BAD_DB_FLAG], *(arg-1));
		    }
		}
		break;

	    case 'v':
	    case 'V':
		DebugFlag |= DB_PRTLINE;
		ShowAllErrors = 1;
		break;

	    case 'b':
	    case 'B':
		PARSENUM(ScFormat, arg);
		if (ScFormat<0 || ScFormat>2) ScFormat=SC_AMPM;
		break;

	    case 'x':
	    case 'X':
		PARSENUM(MaxSatIter, arg);
		if (MaxSatIter < 10) MaxSatIter=10;
		break;

	    case 'k':
	    case 'K':
		MsgCommand = arg;
		while (*arg) arg++;  /* Chew up remaining chars in this arg */
		break;

	    default:
		fprintf(ErrFp, ErrMsg[M_BAD_OPTION], *(arg-1));
	    }

	}
    }

    /* Get the filename. */
    if (!InvokedAsRem) {
	if (i >= argc) {
	    Usage();
	    exit(EXIT_FAILURE);
	}
	InitialFile = argv[i++];
    } else {
	InitialFile = DefaultFilename();
    }

    /* Get the date, if any */
    if (i < argc) {
	while (i < argc) {
	    arg = argv[i++];
	    FindToken(arg, &tok);
	    switch (tok.type) {
	    case T_Time:
		if (SysTime != -1L) Usage();
		else {
		    SysTime = (long) tok.val * 60L;
		    DontQueue = 1;
		    Daemon = 0;
		}
		break;

	    case T_DateTime:
		if (SysTime != -1L) Usage();
		if (m != NO_MON || d != NO_DAY || y != NO_YR || jul != NO_DATE) Usage();
		SysTime = (tok.val % MINUTES_PER_DAY) * 60;
		DontQueue = 1;
		Daemon = 0;
		jul = tok.val / MINUTES_PER_DAY;
		break;

	    case T_Date:
		if (m != NO_MON || d != NO_DAY || y != NO_YR || jul != NO_DATE) Usage();
		jul = tok.val;
		break;

	    case T_Month:
		if (m != NO_MON || jul != NO_DATE) Usage();
		else m = tok.val;
		break;

	    case T_Day:
		if (d != NO_DAY || jul != NO_DATE) Usage();
		else d = tok.val;
		break;

	    case T_Year:
		if (y != NO_YR || jul != NO_DATE) Usage();
		else y = tok.val;
		break;

	    case T_Rep:
		if (rep != NO_REP) Usage();
		else rep = tok.val;
		break;

	    default:
		Usage();
	    }
	}

	if (rep > 0) {
	    Iterations = rep;
	    DontQueue = 1;
	    Daemon = 0;
	}

	if (jul != NO_DATE) {
	    FromJulian(jul, &y, &m, &d);
	}
/* Must supply date in the form:  day, mon, yr OR mon, yr */
	if (m != NO_MON || y != NO_YR || d != NO_DAY) {
	    if (m == NO_MON || y == NO_YR) {
		if (rep == NO_REP) Usage();
		else if (m != NO_MON || y != NO_YR) Usage();
		else {
		    m = CurMon;
		    y = CurYear;
		    if (d == NO_DAY) d = CurDay;
		}
	    }
	    if (d == NO_DAY) d=1;
	    if (d > DaysInMonth(m, y)) {
		fprintf(ErrFp, "%s", BadDate);
		Usage();
	    }
	    JulianToday = Julian(y, m, d);
	    if (JulianToday == -1) {
		fprintf(ErrFp, "%s", BadDate);
		Usage();
	    }
	    CurYear = y;
	    CurMon = m;
	    CurDay = d;
	    if (JulianToday != RealToday) IgnoreOnce = 1;
	}

    }

/* Figure out the offset from UTC */
    if (CalculateUTC)
	(void) CalcMinsFromUTC(JulianToday, SystemTime(0)/60,
			       &MinsFromUTC, NULL);
}

/***************************************************************/
/*                                                             */
/*  Usage                                                      */
/*                                                             */
/*  Print the usage info.                                      */
/*                                                             */
/***************************************************************/
#ifndef L_USAGE_OVERRIDE
void Usage(void)
{
    fprintf(ErrFp, "\nREMIND %s (%s version) Copyright 1992-2022 Dianne Skoll\n", VERSION, L_LANGNAME);
#ifdef BETA
    fprintf(ErrFp, ">>>> BETA VERSION <<<<\n");
#endif
    fprintf(ErrFp, "Usage: remind [options] filename [date] [time] [*rep]\n");
    fprintf(ErrFp, "Options:\n");
    fprintf(ErrFp, " -n     Output next occurrence of reminders in simple format\n");
    fprintf(ErrFp, " -r     Disable RUN directives\n");
    fprintf(ErrFp, " -@[n,m,b] Colorize COLOR/SHADE reminders\n");
    fprintf(ErrFp, " -c[a][n] Produce a calendar for n (default 1) months\n");
    fprintf(ErrFp, " -c[a]+[n] Produce a calendar for n (default 1) weeks\n");
    fprintf(ErrFp, " -w[n[,p[,s]]]  Specify width, padding and spacing of calendar\n");
    fprintf(ErrFp, " -s[a][+][n] Produce `simple calendar' for n (1) months (weeks)\n");
    fprintf(ErrFp, " -p[a][n] Same as -s, but input compatible with rem2ps\n");
    fprintf(ErrFp, " -l     Prefix each simple calendar line with line number and filename comment\n");
    fprintf(ErrFp, " -v     Verbose mode\n");
    fprintf(ErrFp, " -o     Ignore ONCE directives\n");
    fprintf(ErrFp, " -t[n]  Trigger all future (or those within `n' days)\n");
    fprintf(ErrFp, " -h     `Hush' mode - be very quiet\n");
    fprintf(ErrFp, " -a     Don't trigger timed reminders immediately - just queue them\n");
    fprintf(ErrFp, " -q     Don't queue timed reminders\n");
    fprintf(ErrFp, " -f     Trigger timed reminders by staying in foreground\n");
    fprintf(ErrFp, " -z[n]  Enter daemon mode, waking every n (1) minutes.\n");
    fprintf(ErrFp, " -d...  Debug: e=echo x=expr-eval t=trig v=dumpvars l=showline f=tracefiles\n");
    fprintf(ErrFp, " -e     Divert messages normally sent to stderr to stdout\n");
    fprintf(ErrFp, " -b[n]  Time format for cal: 0=am/pm, 1=24hr, 2=none\n");
    fprintf(ErrFp, " -x[n]  Iteration limit for SATISFY clause (def=1000)\n");
    fprintf(ErrFp, " -kcmd  Run `cmd' for MSG-type reminders\n");
    fprintf(ErrFp, " -g[dddd] Sort reminders by date, time, priority, and 'timedness'\n");
    fprintf(ErrFp, " -ivar=val Initialize var to val and preserve var\n");
    fprintf(ErrFp, " -m     Start calendar with Monday rather than Sunday\n");
    fprintf(ErrFp, " -y     Synthesize tags for tagless reminders\n");
    fprintf(ErrFp, " -j[n]  Run in 'purge' mode.  [n = INCLUDE depth]\n");
    exit(EXIT_FAILURE);
}
#endif /* L_USAGE_OVERRIDE */
/***************************************************************/
/*                                                             */
/*  ChgUser                                                    */
/*                                                             */
/*  Run as a specified user.  Can only be used if Remind is    */
/*  started by root.  This changes the real and effective uid, */
/*  the real and effective gid, and sets the HOME, SHELL and   */
/*  USER environment variables.                                */
/*                                                             */
/***************************************************************/
static void ChgUser(char const *user)
{
    uid_t myeuid;

    struct passwd *pwent;
    static char *home;
    static char *shell;
    static char *username;
    static char *logname;

    myeuid = geteuid();

    pwent = getpwnam(user);

    if (!pwent) {
	fprintf(ErrFp, ErrMsg[M_BAD_USER], user);
	exit(EXIT_FAILURE);
    }

    if (!myeuid) {
        /* Started as root, so drop privileges */
#ifdef HAVE_INITGROUPS
        if (initgroups(pwent->pw_name, pwent->pw_gid) < 0) {
            fprintf(ErrFp, ErrMsg[M_NO_CHG_GID], pwent->pw_gid);
            exit(EXIT_FAILURE);
        };
#endif
        if (setgid(pwent->pw_gid) < 0) {
            fprintf(ErrFp, ErrMsg[M_NO_CHG_GID], pwent->pw_gid);
            exit(EXIT_FAILURE);
        }

        if (setuid(pwent->pw_uid) < 0) {
            fprintf(ErrFp, ErrMsg[M_NO_CHG_UID], pwent->pw_uid);
            exit(EXIT_FAILURE);
        }
    }

    home = malloc(strlen(pwent->pw_dir) + 6);
    if (!home) {
	fprintf(ErrFp, "%s", ErrMsg[M_NOMEM_ENV]);
	exit(EXIT_FAILURE);
    }
    sprintf(home, "HOME=%s", pwent->pw_dir);
    putenv(home);

    shell = malloc(strlen(pwent->pw_shell) + 7);
    if (!shell) {
	fprintf(ErrFp, "%s", ErrMsg[M_NOMEM_ENV]);
	exit(EXIT_FAILURE);
    }
    sprintf(shell, "SHELL=%s", pwent->pw_shell);
    putenv(shell);

    if (pwent->pw_uid) {
	username = malloc(strlen(pwent->pw_name) + 6);
	if (!username) {
	    fprintf(ErrFp, "%s", ErrMsg[M_NOMEM_ENV]);
	    exit(EXIT_FAILURE);
	}
	sprintf(username, "USER=%s", pwent->pw_name);
	putenv(username);
	logname= malloc(strlen(pwent->pw_name) + 9);
	if (!logname) {
	    fprintf(ErrFp, "%s", ErrMsg[M_NOMEM_ENV]);
	    exit(EXIT_FAILURE);
	}
	sprintf(logname, "LOGNAME=%s", pwent->pw_name);
	putenv(logname);
    }
}

static void
DefineFunction(char const *str)
{
    Parser p;
    int r;

    CreateParser(str, &p);
    r = DoFset(&p);
    DestroyParser(&p);
    if (r != OK) {
	fprintf(ErrFp, "-i option: %s: %s\n", str, ErrMsg[r]);
    }
}
/***************************************************************/
/*                                                             */
/*  InitializeVar                                              */
/*                                                             */
/*  Initialize and preserve a variable                         */
/*                                                             */
/***************************************************************/
static void InitializeVar(char const *str)
{
    char const *expr;
    char const *ostr = str;
    char varname[VAR_NAME_LEN+1];

    Value val;

    int r;

    /* Scan for an '=' sign */
    r = 0;
    while (*str && *str != '=') {
	if (r < VAR_NAME_LEN) {
	    varname[r++] = *str;
	}
	if (*str == '(') {
	    /* Do a function definition if we see a paren */
	    DefineFunction(ostr);
	    return;
	}
	str++;
    }
    varname[r] = 0;
    if (!*str) {
	fprintf(ErrFp, ErrMsg[M_I_OPTION], ErrMsg[E_MISS_EQ]);
	return;
    }
    if (!*varname) {
	fprintf(ErrFp, ErrMsg[M_I_OPTION], ErrMsg[E_MISS_VAR]);
	return;
    }
    expr = str+1;
    if (!*expr) {
	fprintf(ErrFp, ErrMsg[M_I_OPTION], ErrMsg[E_MISS_EXPR]);
	return;
    }

    r=EvalExpr(&expr, &val, NULL);
    if (r) {
	fprintf(ErrFp, ErrMsg[M_I_OPTION], ErrMsg[r]);
	return;
    }

    if (*varname == '$') {
	r=SetSysVar(varname+1, &val);
        DestroyValue(val);
	if (r) fprintf(ErrFp, ErrMsg[M_I_OPTION], ErrMsg[r]);
	return;
    }

    r=SetVar(varname, &val);
    if (r) {
	fprintf(ErrFp, ErrMsg[M_I_OPTION], ErrMsg[r]);
	return;
    }
    r=PreserveVar(varname);
    if (r) fprintf(ErrFp, ErrMsg[M_I_OPTION], ErrMsg[r]);
    return;
}

static void
AddTrustedUser(char const *username)
{
    struct passwd *pwent;
    if (NumTrustedUsers >= MAX_TRUSTED_USERS) {
        fprintf(stderr, "Too many trusted users (%d max)\n",
                MAX_TRUSTED_USERS);
        exit(EXIT_FAILURE);
    }

    pwent = getpwnam(username);
    if (!pwent) {
	fprintf(ErrFp, ErrMsg[M_BAD_USER], username);
	exit(EXIT_FAILURE);
    }
    TrustedUsers[NumTrustedUsers] = pwent->pw_uid;
    NumTrustedUsers++;
}


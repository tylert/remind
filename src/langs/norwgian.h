/***************************************************************/
/*                                                             */
/*  NORWGIAN.H                                                 */
/*                                                             */
/*  Support for the Norwegian language.                        */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  This file is Copyright (C) 1993 by Trygve Randen.          */
/*  Remind is Copyright (C) 1992-2022 by Dianne Skoll          */
/*                                                             */
/***************************************************************/

/* The very first define in a language support file must be L_LANGNAME: */
#define L_LANGNAME "Norwegian"

/* Day names */
#define L_SUNDAY "Søndag"
#define L_MONDAY "Mandag"
#define L_TUESDAY "Tirsdag"
#define L_WEDNESDAY "Onsdag"
#define L_THURSDAY "Torsdag"
#define L_FRIDAY "Fredag"
#define L_SATURDAY "Lørdag"

/* Month names */
#define L_JAN "Januar"
#define L_FEB "Februar"
#define L_MAR "Mars"
#define L_APR "April"
#define L_MAY "Mai"
#define L_JUN "Juni"
#define L_JUL "Juli"
#define L_AUG "August"
#define L_SEP "September"
#define L_OCT "Oktober"
#define L_NOV "November"
#define L_DEC "Desember"

/* Today and tomorrow */
#define L_TODAY "i dag"
#define L_TOMORROW "i morgen"

/* The default banner */
#define L_BANNER "Påminnelse for %w, %d. %m, %y%o:"

/* "am" and "pm" */
#define L_AM "am"
#define L_PM "pm"

/* Ago and from now */
#define L_AGO "siden"
#define L_FROMNOW "fra nå"

/* "in %d days' time" */
#define L_INXDAYS "om %d dager"

/* "on" as in "on date..." */
#define L_ON "den"

/* Pluralizing - this is a problem for many languages and may require
   a more drastic fix */
#define L_PLURAL "er"

/* Minutes, hours, at, etc */
#define L_NOW "nå"
#define L_AT "kl."
#define L_MINUTE "minutt"
#define L_HOUR "time"
#define L_IS "er"
#define L_WAS "var"
#define L_AND "og"
/* What to add to make "hour" plural */
#define L_HPLU "r"
/* What to add to make "minute" plural */
#define L_MPLU "er"

/* Define any overrides here, such as L_ORDINAL_OVERRIDE, L_A_OVER, etc.
   See the file dosubst.c for more info. */
#define L_ORDINAL_OVERRIDE              plu = ".";
#define L_A_OVER                        if (altmode == '*') { sprintf(s, "%s, den %d. %s %d", DayName[jul%7], d, MonthName[m], y); } else { sprintf(s, "%s %s, den %d. %s %d", L_ON, DayName[jul%7], d, MonthName[m], y); }
#define L_G_OVER                        if (altmode == '*') { sprintf(s, "%s, den %d. %s", DayName[jul%7], d, MonthName[m]); } else { sprintf(s, "%s %s, den %d. %s", L_ON, DayName[jul%7], d, MonthName[m]); }
#define L_U_OVER                        L_A_OVER
#define L_V_OVER                        L_G_OVER

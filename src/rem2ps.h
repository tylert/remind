/***************************************************************/
/*                                                             */
/*  REM2PS.H                                                   */
/*                                                             */
/*  Define the PostScript prologue                             */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2022 by Dianne Skoll                    */
/*                                                             */
/***************************************************************/

char *PSProlog1[] =
{
    "% This file was produced by Remind and Rem2PS, written by",
    "% Dianne Skoll.",
    "% Remind and Rem2PS are Copyright 1992-2022 Dianne Skoll.",
    "/ISOLatin1Encoding where { pop save true }{ false } ifelse",
    "  /ISOLatin1Encoding [ StandardEncoding 0 45 getinterval aload pop /minus",
    "    StandardEncoding 46 98 getinterval aload pop /dotlessi /grave /acute",
    "    /circumflex /tilde /macron /breve /dotaccent /dieresis /.notdef /ring",
    "    /cedilla /.notdef /hungarumlaut /ogonek /caron /space /exclamdown /cent",
    "    /sterling /currency /yen /brokenbar /section /dieresis /copyright",
    "    /ordfeminine /guillemotleft /logicalnot /hyphen /registered /macron",
    "    /degree /plusminus /twosuperior /threesuperior /acute /mu /paragraph",
    "    /periodcentered /cedilla /onesuperior /ordmasculine /guillemotright",
    "    /onequarter /onehalf /threequarters /questiondown /Agrave /Aacute",
    "    /Acircumflex /Atilde /Adieresis /Aring /AE /Ccedilla /Egrave /Eacute",
    "    /Ecircumflex /Edieresis /Igrave /Iacute /Icircumflex /Idieresis /Eth",
    "    /Ntilde /Ograve /Oacute /Ocircumflex /Otilde /Odieresis /multiply",
    "    /Oslash /Ugrave /Uacute /Ucircumflex /Udieresis /Yacute /Thorn",
    "    /germandbls /agrave /aacute /acircumflex /atilde /adieresis /aring /ae",
    "    /ccedilla /egrave /eacute /ecircumflex /edieresis /igrave /iacute",
    "    /icircumflex /idieresis /eth /ntilde /ograve /oacute /ocircumflex",
    "    /otilde /odieresis /divide /oslash /ugrave /uacute /ucircumflex",
    "    /udieresis /yacute /thorn /ydieresis ] def",
    "{ restore } if",
    "",
    "/reencodeISO { %def",
    "    findfont dup length dict begin",
    "    { 1 index /FID ne { def }{ pop pop } ifelse } forall",
    "    /Encoding ISOLatin1Encoding def",
    "    currentdict end definefont pop",
    "} bind def",
    "/copyFont { %def",
    "    findfont dup length dict begin",
    "    { 1 index /FID ne { def } { pop pop } ifelse } forall",
    "    currentdict end definefont pop",
    "} bind def",
    "",
    "% L - Draw a line",
    "/L {",
    "   newpath moveto lineto stroke",
    "} bind def",
    "% string1 string2 strcat string",
    "% Function: Concatenates two strings together.",
    "/strcat {",
    "         2 copy length exch length add",
    "         string dup",
    "         4 2 roll",
    "         2 index 0 3 index",
    "         putinterval",
    "         exch length exch putinterval",
    "} bind def",
    "% string doheading",
    "/doheading",
    "{",
    "   /monthyr exch def",
    "",
    "   /TitleFont findfont",
    "   TitleSize scalefont setfont",
    "   monthyr stringwidth",
    "   /hgt exch def",
    "   2 div MaxX MinX add 2 div exch sub /x exch def",
    "   MaxY Border sub TitleSize sub /y exch def",
    "   newpath x y moveto monthyr show",
    "   newpath x y moveto monthyr false charpath flattenpath pathbbox",
    "   pop pop Border sub /y exch def pop",
    "   MinX y MaxX y L",
    "   /topy y def",
    "   /HeadFont findfont HeadSize scalefont setfont",
    "% Do the days of the week",
    "   MaxX MinX sub 7 div /xincr exch def",
    "   /x MinX def",
    NULL
};
char *PSProlog2[] =
{
    "  {",
    "     HeadSize x y HeadSize 2 mul sub x xincr add y CenterText",
    "     x xincr add /x exch def",
    "  } forall",
    "  y HeadSize 2 mul sub /y exch def",
    "  MinX y MaxX y L",
    "  /ytop y def /ymin y def",
    "}",
    "def",
    "/CenterText",
    "{",
    "   /maxy exch def",
    "   /maxx exch def",
    "   /miny exch def",
    "   /minx exch def",
    "   /sz exch def",
    "   /str exch def",
    "   str stringwidth pop",
    "   2 div maxx minx add 2 div exch sub",
    "   sz 2 div maxy miny add 2 div exch sub",
    "   moveto str show",
    "} def",
    "% Variables:",
    "% curline - a string holding the current line",
    "% y - current y pos",
    "% yincr - increment to next line",
    "% xleft - left margin",
    "% width - max width.",
    "% EnterOneWord - given a word, enter it into the box.",
    "% string EnterOneWord",
    "/EnterOneWord {",
    "   { EnterOneWordAux",
    "     {exit} if }",
    "   loop",
    "} bind def",
    "% EnterOneWordAux - if the word fits, enter it into box and return true.",
    "% If it doesn't fit, put as much as will fit and return the string and false.",
    "/EnterOneWordAux {",
    "   /word exch def",
    "   /tmpline curline word strcat def",
    "   tmpline stringwidth pop width gt",
    "   {MoveToNewLine}",
    "   {/curline tmpline ( ) strcat def /word () def}",
    "   ifelse",
    "   word () eq",
    "   {true}",
    "   {word false}",
    "   ifelse",
    "} bind def",
    "% MoveToNewLine - move to a new line, resetting word as appropriate",
    "/MoveToNewLine {",
    "   curline () ne",
    "   {newpath xleft y moveto curline show /curline () def /y y yincr add def}   ",
    "   {ChopWord}",
    "   ifelse",
    "} bind def",
    "% ChopWord - word won't fit.  Chop it and find biggest piece that will fit",
    "/ChopWord {",
    "   /curline () def",
    "   /len word length def",
    "   /Fcount len 1 sub def",
    "",
    "   {",
    "     word 0 Fcount getinterval stringwidth pop width le",
    "     {exit} if",
    "     /Fcount Fcount 1 sub def",
    "   } loop",
    "% Got the count.  Display it and reset word",
    "   newpath xleft y moveto word 0 Fcount getinterval show",
    "   /y y yincr add def",
    "   /word word Fcount len Fcount sub getinterval def",
    "} bind def",
    "/FinishFormatting {",
    "   word () ne",
    "   {newpath xleft y moveto word show /word () def",
    "    /curline () def /y y yincr add def}",
    "   {curline () ne",
    "     {newpath xleft y moveto curline show /word () def",
    "      /curline () def /y y yincr add def} if}",
    "   ifelse",
    "} bind def",
    "% FillBoxWithText - fill a box with text",
    "% text-array xleft width yincr y FillBoxWithText new-y",
    "% Returns the new Y-coordinate.",
    "/FillBoxWithText {",
    "   /y exch def",
    "   /yincr exch def",
    "   /width exch def",
    "   /xleft exch def",
    "   /curline () def",
    "   % The last two strings in the word array are actually the PostScript",
    "   % code to execute before and after the entry is printed.",
    "   dup dup",
    "   length 1 sub",
    "   get",
    "   exch",
    "   dup dup",
    "   length 2 sub",
    "   get",
    "   dup length 0 gt",
    "   {cvx exec} {pop} ifelse",
    "   dup length 2 sub 0 exch getinterval",
    "   {EnterOneWord} forall",
    "   FinishFormatting",
    "   dup length 0 gt",
    "   {cvx exec} {pop} ifelse",
    "   y",
    "} bind def",
    "% Variables for calendar boxes:",
    "% ytop - current top position",
    "% ymin - minimum y reached for current row",
    "% border ytop xleft width textarray daynum onright DoCalBox ybot",
    "% Do the entries for one calendar box.  Returns lowest Y-coordinate reached",
    "/DoCalBox {",
    "   /onright exch def",
    "   /daynum exch def",
    "   /textarr exch def",
    "   /wid exch def",
    "   /xl exch def",
    "   /yt exch def",
    "   /border exch def",
    "% Do the day number",
    "   /DayFont findfont DaySize scalefont setfont",
    "   onright 1 eq",
    "   {xl wid add border sub daynum stringwidth pop sub yt border sub DaySize sub moveto daynum show}",
    "   {xl border add yt border sub DaySize sub moveto daynum show}",
    "   ifelse",
    "% Do the text entries.  Precharge the stack with current y pos.",
    "   /ycur yt border sub DaySize sub DaySize sub 2 add def",
    "   /EntryFont findfont EntrySize scalefont setfont",
    "   ycur",
    "   textarr",
    "   { exch 2 sub /ycur exch def xl border add wid border sub border sub EntrySize 2 add neg",
    "     ycur FillBoxWithText }",
    "    forall",
    "} bind def",
    "2 setlinecap",
    "% Define a default PreCal procedure",
    "/PreCal { pop pop } bind def",
    NULL
};

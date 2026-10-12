/*
 * t2h_text.h -- the output templates of t2h.pl
 *
 * Generated from t2h.pl by tools/codegen/extract_t2h_templates.py; every
 * `$variable' interpolation of a Perl heredoc became a %s and the variables
 * appear in the order the format expects them (see the comment above each
 * template).
 */

/* t2h.pl line 114: $BODYSTART (no interpolation) */
static const char *BODYSTART =
    "<BODY LANG=\"\" BGCOLOR=\"#C7C1A7\" TEXT=\"#000000\" LINK=\"#0000FF\" VLINK=\"#800080\" ALINK=\"#FF0000\">\n"
    "<FONT FACE=\"Verdana\" Size=\"2\">\n"
    "";

/* t2h.pl line 120: $HBODYSTART (no interpolation) */
static const char *HBODYSTART =
    "<BODY>\n"
    "<FONT FACE=\"Verdana\" Size=\"2\">\n"
    "";

/* t2h.pl line 125: $BODYEND (no interpolation) */
static const char *BODYEND =
    "</FONT>\n"
    "</BODY>\n"
    "";

/* t2h.pl line 226, print to FULL; arguments: TITLE, TITLE, TITLE, BODYSTART, TITLE, AUTHOR */
static const char *T01 =
    "<HEAD>\n"
    "<TITLE>%s</TITLE>\n"
    "\n"
    "<META NAME=\"description\" CONTENT=\"%s: Table of Contents\">\n"
    "<META NAME=\"keywords\" CONTENT=\"%s: Table of Contents\">\n"
    "<META NAME=\"resource-type\" CONTENT=\"document\">\n"
    "<META NAME=\"distribution\" CONTENT=\"global\">\n"
    "<META NAME=\"Generator\" CONTENT=\"t2h.pl\">\n"
    "\n"
    "</HEAD>\n"
    "\n"
    "%s\n"
    "<H1>%s</H1>\n"
    "<H3>by %s</H3>\n"
    "<A NAME=\"contents\"><H2>Table of Contents</H2></A>\n"
    "<UL>\n"
    "";

/* t2h.pl line 248, print to F; arguments: TITLE, TITLE, TITLE, BODYSTART, TITLE, AUTHOR */
static const char *T02 =
    "<HEAD>\n"
    "<TITLE>%s: Table of Contents</TITLE>\n"
    "\n"
    "<META NAME=\"description\" CONTENT=\"%s: Table of Contents\">\n"
    "<META NAME=\"keywords\" CONTENT=\"%s: Table of Contents\">\n"
    "<META NAME=\"resource-type\" CONTENT=\"document\">\n"
    "<META NAME=\"distribution\" CONTENT=\"global\">\n"
    "<META NAME=\"Generator\" CONTENT=\"t2h.pl\">\n"
    "\n"
    "</HEAD>\n"
    "\n"
    "%s\n"
    "<H1>%s</H1>\n"
    "<H3>by %s</H3>\n"
    "<H2>Table of Contents</H2>\n"
    "<UL>\n"
    "";

/* t2h.pl line 281, print to F; arguments: infile, toc, toc, tochtml */
static const char *T03 =
    "<A HREF=\"%s_%shtml\">%s %s</A><BR>\n"
    "";

/* t2h.pl line 284, print to FULL; arguments: toc, toc, tochtml */
static const char *T04 =
    "<A HREF=\"#%s\">%s   %s</A><BR>\n"
    "";

/* t2h.pl line 290, print to F; arguments: BODYEND */
static const char *T05 =
    "%s\n"
    "</HTML>\n"
    "";

/* t2h.pl line 301, print to F; arguments: TITLE, TITLE, TITLE, HBODYSTART, TITLE, AUTHOR, BODYEND */
static const char *T06 =
    "<HEAD>\n"
    "<TITLE>%s</TITLE>\n"
    "\n"
    "<META NAME=\"description\" CONTENT=\"%s\">\n"
    "<META NAME=\"keywords\" CONTENT=\"%s\">\n"
    "<META NAME=\"resource-type\" CONTENT=\"document\">\n"
    "<META NAME=\"distribution\" CONTENT=\"global\">\n"
    "<META NAME=\"Generator\" CONTENT=\"t2h.pl\">\n"
    "\n"
    "</HEAD>\n"
    "\n"
    "%s\n"
    "<H1>%s</H1>\n"
    "<H3>by %s</H3>\n"
    "</UL>\n"
    "%s\n"
    "</HTML>\n"
    "";

/* t2h.pl line 327, print to F; arguments: (none) */
static const char *T07 =
    "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n"
    "<HTML>\n"
    "<HEAD>\n"
    "<meta name=\"GENERATOR\" content=\"t2h.pl texi to html converter\">\n"
    "<!-- Sitemap 1.0 -->\n"
    "</HEAD><BODY>\n"
    "<OBJECT type=\"text/site properties\">\n"
    "\t<param name=\"ImageType\" value=\"Folder\">\n"
    "</OBJECT>\n"
    "<UL>\n"
    "";

/* t2h.pl line 352, print to F; arguments: tochtml, infile, toc */
static const char *T08 =
    "<LI> <OBJECT type=\"text/sitemap\">\n"
    "<param name=\"Name\" value=\"%s\">\n"
    "<param name=\"Local\" value=\"%s_%shtm\">\n"
    "</OBJECT>\n"
    "";

/* t2h.pl line 359, print to F; arguments: BODYEND */
static const char *T09 =
    "</UL>\n"
    "%s\n"
    "</HTML>\n"
    "";

/* t2h.pl line 371, print to F; arguments: (none) */
static const char *T10 =
    "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n"
    "<HTML>\n"
    "<HEAD>\n"
    "<meta name=\"GENERATOR\" content=\"t2h.pl texi to html converter\">\n"
    "<!-- Sitemap 1.0 -->\n"
    "</HEAD><BODY>\n"
    "<OBJECT type=\"text/site properties\">\n"
    "\t<param name=\"ImageType\" value=\"Folder\">\n"
    "</OBJECT>\n"
    "<UL>\n"
    "";

/* t2h.pl line 385, print to F; arguments: tochtml, infile, toc */
static const char *T11 =
    "<LI> <OBJECT type=\"text/sitemap\">\n"
    "<param name=\"Name\" value=\"%s\">\n"
    "<param name=\"Local\" value=\"%s_%shtm\">\n"
    "</OBJECT>\n"
    "";

/* t2h.pl line 398, print to F; arguments: tochtml, infile, toc */
static const char *T12 =
    "<LI> <OBJECT type=\"text/sitemap\">\n"
    "<param name=\"Name\" value=\"%s\">\n"
    "<param name=\"Local\" value=\"%s_%shtm\">\n"
    "</OBJECT>\n"
    "";

/* t2h.pl line 407, print to F; arguments: BODYEND */
static const char *T13 =
    "</UL>\n"
    "%s\n"
    "</HTML>\n"
    "";

/* t2h.pl line 418, print to F; arguments: infile, infile, infile, infile, TITLE, infile */
static const char *T14 =
    "[OPTIONS]\n"
    "Compatibility=1.1 or later\n"
    "Compiled file=%s.chm\n"
    "Contents file=%s.hhc\n"
    "Default topic=%s_title.htm\n"
    "Display compile progress=No\n"
    "Full-text search=Yes\n"
    "Index file=%s.hhk\n"
    "Language=0x409 English (United States)\n"
    "Title=%s\n"
    "\n"
    "[FILES]\n"
    "%s_title.htm\n"
    "";

/* t2h.pl line 439, print to F; arguments: infile, toc */
static const char *T15 =
    "%s_%shtm\n"
    "";

/* t2h.pl line 443, print to F; arguments: (none) */
static const char *T16 =
    "[INFOTYPES]\n"
    "";

/* t2h.pl line 456, print to F; arguments: TITLE, toc, tochtml, TITLE, tochtml, TITLE, tochtml, BODYSTART */
static const char *T17 =
    "<HEAD>\n"
    "<TITLE>%s: %s %s</TITLE>\n"
    "\n"
    "<META NAME=\"description\" CONTENT=\"%s: %s\">\n"
    "<META NAME=\"keywords\" CONTENT=\"%s: %s\">\n"
    "<META NAME=\"resource-type\" CONTENT=\"document\">\n"
    "<META NAME=\"distribution\" CONTENT=\"global\">\n"
    "<META NAME=\"Generator\" CONTENT=\"t2h.pl\">\n"
    "\n"
    "</HEAD>\n"
    "\n"
    "%s\n"
    "";

/* t2h.pl line 578, print to F; arguments: infile, section, section, tochtml */
static const char *T18 =
    "<A HREF=\"%s_%shtml\">%s %s</A><BR>\n"
    "";

/* t2h.pl line 622, print to F; arguments: STEPLINE, BODYEND */
static const char *T19 =
    "<HR>\n"
    "%s\n"
    "%s\n"
    "</HTML>\n"
    "";

/* t2h.pl line 637, print to F; arguments: TITLE, tochtml, TITLE, tochtml, TITLE, tochtml, HBODYSTART */
static const char *T20 =
    "<HEAD>\n"
    "<TITLE>%s: %s</TITLE>\n"
    "\n"
    "<META NAME=\"description\" CONTENT=\"%s: %s\">\n"
    "<META NAME=\"keywords\" CONTENT=\"%s: %s\">\n"
    "<META NAME=\"resource-type\" CONTENT=\"document\">\n"
    "<META NAME=\"distribution\" CONTENT=\"global\">\n"
    "<META NAME=\"Generator\" CONTENT=\"t2h.pl\">\n"
    "\n"
    "</HEAD>\n"
    "\n"
    "%s\n"
    "";

/* t2h.pl line 700, print to F; arguments: STEPLINE, BODYEND */
static const char *T21 =
    "<HR>\n"
    "%s\n"
    "%s\n"
    "</HTML>\n"
    "";

#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>

#define BUFSIZE 512

static void ConvertConf(FILE* file,FILE *out);
static void usage(void);
static char *getfield(char *);
static void ReplaceQuotes(char *out, char *in);
static void oldParseOneLine(FILE *out, char *in);

main(int argc,char *argv[])
{
  FILE *in;
  FILE *out;

  if(argc < 3)
    usage();

  if (( in = fopen(argv[1],"r")) == (FILE *)NULL )
    {
      fprintf(stderr,"Can't open %s for reading\n", argv[1]);
      usage();
    }

  if (( out = fopen(argv[2],"w")) == (FILE *)NULL )
    {
      fprintf(stderr,"Can't open %s for writing\n", argv[2]);
      usage();
    }
  
  ConvertConf(in,out);
}

static void usage()
{
  fprintf(stderr,"convertconf ircd.conf.old ircd.conf.new\n");
  exit(-1);
}

/*
** convertConf() 
**    Read configuration file.
**
*
* Inputs        - FILE* to config file to convert
*		- FILE* to output for new style conf
*
**    returns -1, if file cannot be opened
**             0, if file opened
*/

#define MAXCONFLINKS 150

static void ConvertConf(FILE* file,FILE *out)
{
  char             line[BUFSIZE];
  char             quotedLine[BUFSIZE];
  char*            p;

  while (fgets(line, sizeof(line), file))
    {
      if ((p = strchr(line, '\n')))
        *p = '\0';

      ReplaceQuotes(quotedLine,line);

      if (!*quotedLine || quotedLine[0] == '#' || quotedLine[0] == '\n' ||
          quotedLine[0] == ' ' || quotedLine[0] == '\t')
        continue;

      if(quotedLine[0] == '.')
        {
          char *filename;
          char *back;

          if(!strncmp(quotedLine+1,"include ",8))
            {
              if( (filename = strchr(quotedLine+8,'"')) )
                filename++;
              else
                {
                  fprintf(stderr, "Bad config line: %s", quotedLine);
                  continue;
                }

              if( (back = strchr(filename,'"')) )
                *back = '\0';
              else
                {
                  fprintf(stderr, "Bad config line: %s", quotedLine);
                  continue;
                }

	    }
	}

      /* Could we test if it's conf line at all?        -Vesa */
      if (quotedLine[1] == ':')
        oldParseOneLine(out,quotedLine);

    }

  fclose(file);
}

/*
 * ReplaceQuotes
 * Inputs       - input line to quote
 * Output       - quoted line
 * Side Effects - All quoted chars in input are replaced
 *                with quoted values in output, # chars replaced with '\0'
 *                otherwise input is copied to output.
 */
static void ReplaceQuotes(char* quotedLine,char *inputLine)
{
  char *in;
  char *out;
  static char  quotes[] = {
    0,    /*  */
    0,    /* a */
    '\b', /* b */
    0,    /* c */
    0,    /* d */
    0,    /* e */
    '\f', /* f */
    0,    /* g */
    0,    /* h */
    0,    /* i */
    0,    /* j */
    0,    /* k */
    0,    /* l */
    0,    /* m */
    '\n', /* n */
    0,    /* o */
    0,    /* p */
    0,    /* q */
    '\r', /* r */
    0,    /* s */
    '\t', /* t */
    0,    /* u */
    '\v', /* v */
    0,    /* w */
    0,    /* x */
    0,    /* y */
    0,    /* z */
    0,0,0,0,0,0 
    };

  /*
   * Do quoting of characters and # detection.
   */
  for (out = quotedLine,in = inputLine; *in; out++, in++)
    {
      if (*in == '\\')
	{
          in++;
          if(*in == '\\')
            *out = '\\';
          else if(*in == '#')
            *out = '#';
	  else
	    *out = quotes[ (unsigned int) (*in & 0x1F) ];
	}
      else if (*in == '#')
        {
	  *out = '\0';
          return;
	}
      else
        *out = *in;
    }
  *out = '\0';
}

/*
 * oldParseOneLine
 * Inputs       - pointer to line to parse
 *		- pointer to output to write
 * Output       - 
 * Side Effects - Parse one old style conf line.
 */

static void oldParseOneLine(FILE *out,char* line)
{
  char conf_letter;
  char* tmp;
  char* user_field=(char *)NULL;
  char* passwd_field=(char *)NULL;
  char* host_field=(char *)NULL;
  char* port_field=(char *)NULL;
  char* class_field=(char *)NULL;
  int   sendq = 0;

  tmp = getfield(line);

  conf_letter = *tmp;

  for (;;) /* Fake loop, that I can use break here --msa */
    {
      /* host field */
      if ((host_field = getfield(NULL)) == NULL)
	break;
      
      /* pass field */
      if ((passwd_field = getfield(NULL)) == NULL)
	break;

      /* user field */
      if ((user_field = getfield(NULL)) == NULL)
	break;

      /* port field */
      if ((port_field = getfield(NULL)) == NULL)
	break;

      /* class field */
      if ((class_field = getfield(NULL)) == NULL)
	break;
      
      break;
      /* NOTREACHED */
    }

  switch( conf_letter )
    {
    case 'A':case 'a': /* Name, e-mail address of administrator */
      fprintf(out,"\tadminstrator {\n");
      fprintf(out,"\t\tname=\"%s\"\n;", host_field);
      fprintf(out,"\t\temail=\"%s\"\n;", user_field);
      fprintf(out,"\t};\n\n");
      break;

    case 'c':
      /* drop into normal C line code */

    case 'C':
      break;

    case 'd':
      fprintf(out,"\tacl_exception {\n");
      fprintf(out,"\t\tip=\"%s\"\n;", user_field);
      fprintf(out,"\t\treason=\"%s\";\n", passwd_field);
      fprintf(out,"\t};\n\n");
      break;

    case 'D': /* Deny lines (immediate refusal) */
      fprintf(out,"\tacl {\n");
      fprintf(out,"\t\tip=\"%s\";\n", user_field);
      fprintf(out,"\t\treason=\"%s\";\n", passwd_field);
      fprintf(out,"\t};\n\n");
      break;

    case 'H': /* Hub server line */
    case 'h':
      fprintf(out,"\thub {\n");
      fprintf(out,"\t\tname=\"%s\";\n", user_field);
      fprintf(out,"\t};\n\n");
      break;

    case 'i': /* Just plain normal irc client trying  */
                  /* to connect to me */

      /* drop into normal I line code */

    case 'I': /* Just plain normal irc client trying  */
      /* to connect to me */

      fprintf(out,"\tclient {\n");
      fprintf(out,"\t\tname=\"%s\";\n", user_field);
      fprintf(out,"\t};\n\n");
      
      if(host_field)
	{
	}
      
      if(user_field)
	{
#if 0
	  user_field = set_conf_flags(aconf, user_field);
#endif
	}

#if 0
      conf_add_i_line(aconf,class_field);
#endif
      break;
      
    case 'K': /* Kill user line on irc.conf           */
    case 'k':
      fprintf(out,"\tkill {\n");
      fprintf(out,"\t\tname=\"%s@%s\";\n", user_field,host_field);
      fprintf(out,"\t\treason=\"%s\"\n;", passwd_field);
      fprintf(out,"\t};\n\n");
      break;

    case 'L': /* guaranteed leaf server */
    case 'l':
      break;

      /* Me. Host field is name used for this host */
      /* and port number is the number of the port */
    case 'M':
    case 'm':
      if(port_field)
        {
	}
      break;

    case 'n': /* connect in case of lp failures     */
      /* drop into normal N line code */

    case 'N': /* Server where I should NOT try to     */
      /* but which tries to connect ME        */
      break;

      /* Operator. Line should contain at least */
      /* password and host where connection is  */
    case 'O':
      /* defaults */
      fprintf(out,"\toperator {\n");
      fprintf(out,"\t\tname=\"%s\";\n", user_field);
      fprintf(out,"\t\thost=\"%s\";\n", host_field);
      fprintf(out,"\t\tpassword=\"%s\";\n", passwd_field);
      fprintf(out,"\t};\n\n");
#if 0
      aconf->port = 
	CONF_OPER_GLOBAL_KILL|CONF_OPER_REMOTE|CONF_OPER_UNKLINE|
	CONF_OPER_K|CONF_OPER_GLINE|CONF_OPER_REHASH;
      if(port_field)
	aconf->port = oper_privs_from_string(aconf->port,port_field);
      if ((tmp = getfield(NULL)) != NULL)
	aconf->hold = oper_flags_from_string(tmp);
      aconf = conf_add_o_line(aconf,class_field);
#endif
      break;

      /* Local Operator, (limited privs --SRB) */
    case 'o':
#if 0
      aconf->port = CONF_OPER_UNKLINE|CONF_OPER_K;
      if(port_field)
	aconf->port = oper_privs_from_string(aconf->port,port_field);
      if ((tmp = getfield(NULL)) != NULL)
	aconf->hold = oper_flags_from_string(tmp);
      aconf = conf_add_o_line(aconf,class_field);
#endif
      break;

    case 'P': /* listen port line */
    case 'p':
#if 0
      conf_add_port(aconf);
#endif
      break;

    case 'Q': /* reserved nicks */
    case 'q': 
      break;

    case 'U': /* Uphost, ie. host where client reading */
    case 'u': /* this should connect.                  */
      break;

    case 'X': /* rejected gecos */
    case 'x': 
      break;

    case 'Y':
    case 'y':
      if(class_field)
	sendq = atoi(class_field);
      break;
      
    default:
      fprintf(stderr, "Error in config file: %s", line);
      break;
    }
}

/*
 * field breakup for ircd.conf file.
 */
static char *getfield(char *newline)
{
  static char *line = (char *)NULL;
  char  *end, *field;
        
  if (newline)
    line = newline;

  if (line == (char *)NULL)
    return((char *)NULL);

  field = line;
  if ((end = strchr(line,':')) == NULL)
    {
      line = (char *)NULL;
      if ((end = strchr(field,'\n')) == (char *)NULL)
        end = field + strlen(field);
    }
  else
    line = end + 1;
  *end = '\0';
  return(field);
}

#include <stdio.h>
#include <stdlib.h>

#include "getopt.h"

void
parseargs(int *argc, char ***argv, struct lgetopt *opts)
{
  int i;
  char *progname = (*argv)[0];

  /* loop through each argument */
  for (;;)
    {
      (*argc)--;
      (*argv)++;
      
      if (*argc < 1)
	{
	  return;
	}
      
      /* check if it *is* an arg.. */
      if ((*argv)[0][0] != '-')
	{
	  return;
	}
      
      /* search through our argument list, and see if it matches */
      for (i = 0; opts[i].opt; i++) 
	{
	  if (!strcmp(opts[i].opt, (*argv)[0] + 1))
	    {
	      /* found our argument */
	      switch (opts[i].argtype)
		{
		case YESNO:
		  *((int *)opts[i].argloc) = 1;
		  break;
		case INTEGER:
		  if (*argc < 2)
		    {
		      fprintf(stderr, "Error: option '-%s' requires an argument\n",
			      opts[i].opt);
		      usage((*argv)[0]);
		    }
		  
		  *((int *)opts[i].argloc) = atoi((*argv)[1]);
		  break;
		case STRING:
		  if (*argc < 2)
		    {
		      fprintf(stderr, "error: option '-%s' requires an argument\n",
			      opts[i].opt);
		      usage((*argv)[0]);
		    }
		  
		  (char *)opts[i].argloc = malloc(strlen((*argv)[1]) + 1);
		  strcpy((char *)opts[i].argloc, (*argv)[1]);
		  break;
		case USAGE:
		  usage(progname);
		  /*NOTREACHED*/
		default:
		  fprintf(stderr, "Error: internal error in parseargs() at %s:%d\n",
			  __FILE__, __LINE__);
		  exit(EXIT_FAILURE);
		}
	    }
	}
    }
}

void 
usage(char *name)
{
  int i = 0;
  
  fprintf(stderr, "Usage: %s [options] host\n", name);
  fprintf(stderr, "Where valid options are:\n");
  
  for (i = 0; myopts[i].opt; i++)
    {
      fprintf(stderr, "\t-%-10s %-20s%s\n", myopts[i].opt, 
	      (myopts[i].argtype == YESNO || myopts[i].argtype == USAGE) ? "" : 
	      myopts[i].argtype == INTEGER ? "[Integer]" : "[String]",
	      myopts[i].desc);
    }
  
  exit(EXIT_FAILURE);
}


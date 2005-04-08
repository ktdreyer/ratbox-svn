/*
 * mem.c $Id$
 */

#include <stdlib.h>


void *MyMalloc(size_t size)
{
        void *ptr;  
        ptr = calloc(1, size);
        if(ptr == NULL)
        {
                exit(1);
        }
        return(ptr);
}

void MyFree(void *ptr)
{
        if(ptr != NULL)
                free(ptr);   
}


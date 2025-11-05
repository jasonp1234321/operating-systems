/* lecex3-q1-main.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hidden-header-file.h"

/* The lecex3_q1_parent() function is hidden in hidden-header-file.h */
int lecex3_q1_parent( int pipefd );

/* You will write the lecex3_q1_child() function in lecex3-q1.c */
int lecex3_q1_child( int pipefd );

int main()
{
  int * p = calloc( 2, sizeof( int ) );
  if ( p == NULL ) { perror( "calloc() failed" ); return EXIT_FAILURE; }

  int pipe_rc = pipe( p );
  if ( pipe_rc == -1 ) { perror( "pipe() failed" ); return EXIT_FAILURE; }

  int rc = EXIT_SUCCESS;
  pid_t pid = fork();
  if ( pid == -1 ) { perror( "fork() failed" ); return EXIT_FAILURE; }

  if ( pid > 0 )
  {
    close( *p );  /* close the read end of the pipe */
    rc = lecex3_q1_parent( *(p + 1) );
  }
  else
  {
    close( *(p + 1) );  /* close the write end of the pipe */
    rc = lecex3_q1_child( *p );
  }

  free( p );

  return rc;
}

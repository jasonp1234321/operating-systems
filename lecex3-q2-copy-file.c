/* lecex3-q2-copy-file.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#define BUFSIZE 256

void * copy_file( void * arg )
{
  /* return value from this thread is the number of bytes copied */
  int * rc = calloc( 1, sizeof( int ) );

  int infd, outfd;
  int bytes_written = 0;

  printf( "CHILD THREAD: Copying \"%s\"...\n", (char *)arg );

  infd = open( arg, O_RDONLY );
  if ( infd == -1 ) { perror( "CHILD THREAD: open() failed to read existing file" ); pthread_exit( rc ); }

  char * outfilename = malloc( strlen( "backup_" ) + strlen( arg ) + 1 );
  strcpy( outfilename, "backup_" );
  strcat( outfilename, arg ); 

  outfd = open( outfilename, O_WRONLY | O_CREAT | O_TRUNC, 0664 );
  free( outfilename );
  if ( outfd == -1 ) { perror( "CHILD THREAD: open() failed to create backup file" ); pthread_exit( rc ); }

  char * buffer = calloc( BUFSIZE, sizeof( char ) );

  while ( 1 )
  {
    int bytes_read = read( infd, buffer, BUFSIZE );

    if ( bytes_read == -1 ) { perror( "CHILD THREAD: read() failed" ); break; }
    else if ( bytes_read == 0 ) break;

    bytes_written = write( outfd, buffer, bytes_read );

    if ( bytes_written != bytes_read ) { perror( "CHILD THREAD: write() failed" ); break; }
    *rc += bytes_written;
  }

  free( buffer );
  close( infd );
  close( outfd );
  pthread_exit( rc );
}

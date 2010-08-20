#include <math.h>
#include "avisynth.h"
#include "stdlib.h"
#include "memory.h"
#include "winsock2.h"

#define DEFAULT_PORT 17366
//#define BIND_TO_PORT 17366

typedef struct {
  AVS_FilterInfo  *fi;
  WSADATA         *wsaData; 
	SOCKADDR_IN     *target_addr;   
#ifdef BIND_TO_PORT
	SOCKADDR_IN     *local_addr;
#endif	
	SOCKET					socket_handle;
} sender_data;

// Destroys any objects created in create_sender

void AVSC_CC destroy_sender ( AVS_FilterInfo * fi ) {
  sender_data *f = ( sender_data* )fi->user_data;

	if( f->socket_handle != INVALID_SOCKET ) {
		closesocket( f->socket_handle );
	}

  free( f->wsaData );
  free( f->target_addr );
#ifdef BIND_TO_PORT
  free( f->local_addr );
#endif
  free( f );

  WSACleanup();
  
  fi->user_data = 0;


}

// Applies colour curve to colour triplet

void curve( float *r, float *g, float *b ) {
	*r *= *r;
	*g *= *g;
	*b *= *b;
}

// Applies dithering to colour triplet

void dither( float *r, float *g, float *b, unsigned char *rr, unsigned char *gg, unsigned char *bb ) {
	*r *= 255;
	*g *= 255;
	*b *= 255;
	*rr = ( ( unsigned char )floor( *r ) ) & 0xFC ;
	if( ( ( *r - ( float )*rr ) / 4.0 ) > ( ( float )rand() / ( float )RAND_MAX ) ) *rr = ( *rr < 252 ? *rr + 4 : 252 );
	*gg = ( ( unsigned char )floor( *g ) ) & 0xFC ;
	if( ( ( *g - ( float )*gg ) / 4.0 ) > ( ( float )rand() / ( float )RAND_MAX ) ) *gg = ( *gg < 252 ? *gg + 4 : 252 );
	*bb = ( ( unsigned char )floor( *b ) ) & 0xFC ;
	if( ( ( *b - ( float )*bb ) / 4.0 ) > ( ( float )rand() / ( float )RAND_MAX ) ) *bb = ( *bb < 252 ? *bb + 4 : 252 );
	
	//"uncurve"
	//*rr = ( unsigned char )floor( sqrt( ( float )*rr / 255.0 ) * 255.0 );
	//*gg = ( unsigned char )floor( sqrt( ( float )*gg / 255.0 ) * 255.0 );
	//*bb = ( unsigned char )floor( sqrt( ( float )*bb / 255.0 ) * 255.0 );
}

// Temporary test function, returns a modified frame

AVS_VideoFrame * AVSC_CC sender( AVS_FilterInfo * p, int n ) {
  AVS_VideoFrame * frame;
  int row_size, height, pitch;
  BYTE * data;
  int y, x;
  float r,g,b;
  unsigned char pkt[ 1024 ] = "sync", *ppkt = pkt;

  sender_data *f = ( sender_data* )p->user_data;

	sendto( f->socket_handle, pkt, 4, 0, ( SOCKADDR* )f->target_addr, sizeof( SOCKADDR_IN ) );   

  frame    = avs_get_frame( p->child, n );
  avs_make_writable( p->env, &frame );
  row_size = avs_get_row_size( frame );
  height   = avs_get_height( frame );
  pitch    = avs_get_pitch( frame );
  data     = avs_get_write_ptr( frame ) + pitch * ( height - 1 );

  for (y = 0; y != height; ++y) {
	  for (x = 0; x < row_size; x += 3 ) {
			r = ( ( float )data[ x + 0 ] ) / 255;
    	g = ( ( float )data[ x + 1 ] ) / 255;
    	b = ( ( float )data[ x + 2 ] ) / 255;
    	curve( &r, &g, &b );
    	dither( &r, &g, &b, &data[ x + 0 ], &data[ x + 1 ], &data[ x + 2 ] );
    	*ppkt++ = data[ x + 2 ];
    	*ppkt++ = data[ x + 1 ];
    	*ppkt++ = data[ x + 0 ];
    	*ppkt++ = data[ x + 0 ];
    	if( ppkt == &pkt[ 1024 ] ) {
				sendto( f->socket_handle, pkt, 1024, 0, ( SOCKADDR* )f->target_addr, sizeof( SOCKADDR_IN ) );   
				ppkt = pkt;
    	}
    }  
    data -= pitch;
  }
  return frame;  
}


// Decrunches an IP[:port] string into an SOCKADDR_IN struct

static int set_addr( SOCKADDR_IN *sa, const char *addr ) {
	const char *a = addr;
	unsigned char net = 0, sub = 0, ip[ 4 ];
	unsigned short port = 0;
	memset( sa, 0, sizeof( SOCKADDR_IN ) );
	sa->sin_family = AF_INET;
	do {
	
		if( *a == '.' || *a == ':' || *a == 0 ) {
			ip[ sub ] = net;
			net = 0;
			if( *a == '.' && sub == 3 ) return 0;
			if( ( *a == ':' || *a == 0 ) && sub != 3 ) return 0;
			if( *a == ':' ) {
				sa->sin_addr.s_addr = *( ( int* )ip );
				a++;
				while( *a != 0 ) {
					if( *a < '0' || *a > '9' ) return 0;
					if( port > 6553 ) return 0;
					port *= 10;
					if( 65535 - port < *a - '0' ) return 0;
					port += *a - '0';
					a++;
				}
				if( port == 0 ) return 0;
				sa->sin_port = htons( port );
				return 1;
			} else if( *a == 0 ) {
				sa->sin_addr.s_addr = *( ( int* )ip );
				sa->sin_port = htons( DEFAULT_PORT );
				return 1;
			}
			sub++;
		} else {
			if( *a < '0' || *a > '9' ) return 0;
			if( net > 25 ) return 0;
			net *= 10;
			if( 255 - net < *a - '0' ) return 0;
			net += *a - '0';
		}
	} while( *a++ != 0 );
	return 0;
}

// Creates filter instance

AVS_Value AVSC_CC create_sender( AVS_ScriptEnvironment * env, AVS_Value args, void * user_data ) {
  AVS_Value v, tmp;
  sender_data *f = ( sender_data* )malloc( sizeof( sender_data ) );
  AVS_Clip * new_clip = avs_new_c_filter( env, &f->fi, avs_array_elt( args, 0 ), 1 );
  
  f->fi->user_data = f;
  
  f->wsaData = ( WSADATA* )malloc( sizeof( WSADATA ) );
  f->target_addr = ( SOCKADDR_IN* )malloc( sizeof( SOCKADDR_IN ) );
#ifdef BIND_TO_PORT
  f->local_addr = ( SOCKADDR_IN* )malloc( sizeof( SOCKADDR_IN ) );
#endif
	f->socket_handle = INVALID_SOCKET;

  if( WSAStartup( MAKEWORD( 2, 2 ), f->wsaData ) != 0 ) {   

    v = avs_new_value_error( "WSAStartup failed" );

  } else {

		f->socket_handle = socket( PF_INET, SOCK_DGRAM, 0 );

		if( f->socket_handle == INVALID_SOCKET ) {
			
			v = avs_new_value_error( "Socket failed" );
			
		} else {

			tmp = avs_array_elt( args, 1 ); // IP:port
			if( ( !avs_defined( tmp ) ) || !avs_is_string( tmp ) ) {

				v = avs_new_value_error( "Argument addr must not be ommited" );

			} else {

				if( !set_addr( f->target_addr, avs_as_string( tmp ) ) ) {
					
					v = avs_new_value_error( "Invalid argument: addr - ip.ip.ip.ip[:port]" );				
				
				} else {

#ifdef BIND_TO_PORT
					memset( f->local_addr, 0, sizeof( SOCKADDR_IN ) );
					f->local_addr->sin_family = AF_INET;
					f->local_addr->sin_addr.s_addr = htonl( INADDR_ANY );
					f->local_addr->sin_port = htons( BIND_TO_PORT );
				
					if( bind( f->socket_handle, ( SOCKADDR* ) f->local_addr, sizeof( SOCKADDR_IN ) ) == SOCKET_ERROR ) {
										
						v = avs_new_value_error( "Bind failed" );
					
					} else {
#endif
						if( avs_is_rgb24( &f->fi->vi ) && avs_row_size( &f->fi->vi ) == 192 && avs_bmp_size( &f->fi->vi ) == 9216 ) {
						
							// Set get_frame if all is OK
							f->fi->get_frame = sender;
							v = avs_new_value_clip( new_clip );
						
						} else {
						
							v = avs_new_value_error( "Video must be RGB24, 64x48 pixels" );
							
						}
						
#ifdef BIND_TO_PORT
					}
#endif
				}
				
			}
	  }
	}

  avs_release_clip(new_clip);
  f->fi->free_filter = destroy_sender;
  return v;
}

// Initializes plugin, adds available functions

const char * AVSC_CC avisynth_c_plugin_init (AVS_ScriptEnvironment * env) {
  avs_add_function( env, "send_frame", "c[addr]s", create_sender, 0 );
  return "PET-GCM Video Stream Source";
}

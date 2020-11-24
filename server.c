#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/md5.h>
#include <signal.h>

struct user{
	char* account;
	char* passwd_MD5;
	int online;
	int id;
};

struct state{
	char* name;
	int try_login;
	int gaming;
	int invited;
	int oppos, board_idx, player_idx;
	int next_round, verify;
	int win, lose;
	int watching[5], cnt, watch;
};

struct board_items{
	char play_board[3][3];
	int players_score[2];
	int turn;
	int cnt;
	int w, l;
};

struct user* list[10000];
struct state* table[10000];
struct board_items* board_array[10000];

unsigned long hash( unsigned char* str );
char *str2md5( const char *str, int length );
int check( const char board[3][3] );
void menu( int fd );
void drop( int fd, fd_set* m );
void init( int fd );
void logo( int fd );
void sighandler_ctrlc();

fd_set master;
int max_socket;
int socket_listen;

int main(){
	signal( SIGINT, sighandler_ctrlc );
	printf("Configuring local address...\n");

	struct addrinfo hints;
	
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_flags = AI_PASSIVE;	// ready to bind
	hints.ai_family = AF_INET;	// ipv4
	hints.ai_socktype = SOCK_STREAM;	// tcp

	struct addrinfo* bind_address;
	getaddrinfo( 0, "8080", &hints, &bind_address );

	printf("Creating socket...\n");
	socket_listen = socket( bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol );

	if( socket_listen < 0 ){
		perror("socket() failed");
		exit(1);
	}

	printf("Binding socket to local address...\n");

	if( bind( socket_listen, bind_address->ai_addr, bind_address->ai_addrlen ) == -1 ){
		perror("bind() failed");
		exit(1);
	}

	printf("Listening...\n");
	if( listen( socket_listen, 10 ) == -1 ){
		perror("listen() failed");
		exit(1);
	}

	FD_ZERO( &master );
	FD_SET( socket_listen, &master );
	max_socket = socket_listen;

	printf("Waiting for connections...\n");

	/* ---------------------------------- */
	FILE* fp = fopen( "./user.db", "r" );
	if( fp == NULL ){
		perror("fopen failed: ");
		exit(1);
	}
	
	char account[16], passwd_MD5[33];
	while( ( fscanf( fp, "%s %s", account, passwd_MD5 ) ) != EOF ){
		struct user* temp = (struct user*)malloc( sizeof( struct user ) );
		temp->account = (char*)malloc( 16 );
		memset( temp->account, '\0', 16 );
		temp->passwd_MD5 = (char*)malloc( 33 );
		memset( temp->passwd_MD5, '\0', 33 );

		strcpy( temp->account, account );
		strcpy( temp->passwd_MD5, passwd_MD5 );

		temp->online = 0;
		temp->id = -1;

		unsigned long h = hash( temp->account ) % 10000;
		list[h] = temp;

		//fprintf( stderr, "h: %ld, account: %s, passwd_MD5: %s\n", h, list[h]->account, list[h]->passwd_MD5 );
	}
	fclose( fp );
	/* ---------------------------------- */

	while(1){
		fd_set rset;
		rset = master;
		
		if( select( max_socket + 1, &rset, 0, 0, 0 ) == -1 ){
			perror("select() failed");
			exit(1);
		}

		for( int i = 1; i <= max_socket; i++ ){
			unsigned long h;
			if( FD_ISSET( i, &rset ) ){
				// --------------------------------- server
				if( i == socket_listen ){
					struct sockaddr_storage client_address;
					socklen_t client_len = sizeof( client_address );
				
					int socket_client = accept( socket_listen, (struct sockaddr*) &client_address, &client_len );
				
					if( socket_client == -1 ){
						perror("accept fail()");
						exit(1);
					}
				
					FD_SET( socket_client, &master );
					if( socket_client > max_socket )
						max_socket = socket_client;

					char address_buffer[100];
					getnameinfo( (struct sockaddr*) &client_address, client_len, address_buffer, sizeof( address_buffer ), 0, 0, NI_NUMERICHOST );
				
					printf( "New connection from %s\n" , address_buffer );
					logo( socket_client );
					send( socket_client, "Please enter your account or the account you want to register:\n", 63, 0 ); 
				}

				// --------------------------------- client
				else{
					char read[1024];
					int bytes_received = recv( i, read, 1024, 0 );
					if( bytes_received < 1 ){
						FD_CLR( i, &master );
						close( i );
						continue;
					}
					read[bytes_received-1] = '\0';
					
					// --------------------------------- log in
					if( table[i] == NULL ){
						table[i] = (struct state*)malloc( sizeof( struct state ) );
						table[i]->name = (char*)malloc( 16 );
						strcpy( table[i]->name, read );
						table[i]->try_login = 1;
						
						fprintf( stderr, "Account: %s want to login\n", table[i]->name );
						send( i, "Please enter your password:\n", 28, 0 );
					}

					else if( table[i] != NULL ){
						h = hash( table[i]->name ) % 10000;
						
						if( table[i]->try_login == 1 ){
							char passwd[16];
							strcpy( passwd, read );
						
							char* md5 = str2md5( passwd, strlen( passwd ) );

							if( list[h] != NULL && !strcmp( md5, list[h]->passwd_MD5 ) && list[h]->online == 0 ){
								logo( i );
								char buffer[64] = {0};
								sprintf( buffer, "Log in successfully. Wellcome, %s!\n", list[h]->account );
								send( i, buffer, strlen( buffer ), 0 );
								send( i, "\n", 1, 0 );
								menu( i );
								fprintf( stderr, "Account: %s logged in\n", table[i]->name );
								
								list[h]->online = 1;
								list[h]->id = i;
								
								table[i]->try_login = 0;
								table[i]->win = 0;
								table[i]->lose = 0;
								init( i );
							}
							else if( list[h] != NULL && !strcmp( md5, list[h]->passwd_MD5 ) && list[h]->online == 1 ){
								send( i, "\n", 1, 0 );
								send( i, "This account is already logged in\n", 34, 0 );
								fprintf( stderr, "Account: %s already logged in\n", table[i]->name );
								
								table[i]->try_login = 0;
								drop( i, &master );
								continue;
							}
							else if( list[h] == NULL ){
								FILE* fp = fopen( "./user.db", "a" );
								if( fp == NULL )
									perror("fopen failed: ");
								else
									fprintf( fp, "%s %s\n", table[i]->name, md5 );
								
								fclose( fp );

								struct user* temp = (struct user*)malloc( sizeof( struct user ) );
								temp->account = (char*)malloc( 16 );
								memset( temp->account, '\0', 16 );
								temp->passwd_MD5 = (char*)malloc( 33 );
								memset( temp->passwd_MD5, '\0', 33 );

								strcpy( temp->account, table[i]->name );
								strcpy( temp->passwd_MD5, md5 );
								
								temp->online = 1;
								temp->id = i;
								list[h] = temp;
								
								logo( i );
								char buffer[64] = {0};
								sprintf( buffer, "Register successfully. Wellcome, %s!\n", list[h]->account );
								send( i, buffer, strlen( buffer ), 0 );
								send( i, "\n", 1, 0 );
								menu( i );
								fprintf( stderr, "Account: %s registered and logged in\n", table[i]->name );
								
								table[i]->try_login = 0;
								table[i]->win = 0;
								table[i]->lose = 0;
								init( i );
							}
						}
						else if( table[i]->gaming == 1 && table[i]->next_round == 0 ){
							int oppos = table[i]->oppos;
							int idx = table[i]->board_idx;
							int pos = atoi( read );
							int row, col;
							( pos % 3 == 0 ) ? ( row = pos / 3 - 1 ) : ( row = pos / 3 );
							( pos % 3 == 0 ) ? ( col = 2 ) : ( col = pos % 3 - 1 ); 

							if( board_array[idx]->turn != i )
								continue;

							board_array[idx]->turn = oppos;
							( table[i]->player_idx == 1 ) ? ( board_array[idx]->play_board[row][col] = 'X' ) : ( board_array[idx]->play_board[row][col] = 'O' );
							board_array[idx]->cnt -= 1;
							
							char buffer[256] = {0}, temp[32] = {0};
							strcat( buffer, " _________________\n" );
							strcat( buffer, "|     |     |     | \n" );
							sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[idx]->play_board[0][0], board_array[idx]->play_board[0][1], board_array[idx]->play_board[0][2] );
							strcat( buffer, temp );
									
							strcat( buffer, "|_____|_____|_____|\n" );
							strcat( buffer, "|     |     |     | \n" );
							sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[idx]->play_board[1][0], board_array[idx]->play_board[1][1], board_array[idx]->play_board[1][2] );
							strcat( buffer, temp );
									
							strcat( buffer, "|_____|_____|_____|\n" );
							strcat( buffer, "|     |     |     | \n" );
							sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[idx]->play_board[2][0], board_array[idx]->play_board[2][1], board_array[idx]->play_board[2][2] );
							strcat( buffer, temp );
									
							strcat( buffer, "|_____|_____|_____|\n" );

							send( i, buffer, strlen( buffer ), 0 );
							send( oppos, buffer, strlen( buffer ), 0 );
							
							for( int k = 0; k < 5; k++ ){
								if( table[i]->watching[k] != 0 )
									send( table[i]->watching[k], buffer, strlen( buffer ), 0 );
								if( table[oppos]->watching != 0 )
									send( table[oppos]->watching[k], buffer, strlen( buffer ), 0 );
							}
							
							memset( buffer, '\0', 256 );
							sprintf( buffer, "%s, please enter the number of the square where you want to place your %c:\n", table[oppos]->name, ( table[oppos]->player_idx )?'X':'O' );
							send( oppos, buffer, strlen( buffer ), 0 );

							memset( buffer, '\0', 256 );
							sprintf( buffer, "Waiting for %s...\n", table[oppos]->name );
							send( i, buffer, strlen( buffer ), 0 );
							
							int ret = check( board_array[idx]->play_board );
							if( ret == 0 || ret == 1 ){
								int winner, loser;

								if( table[i]->player_idx == ret ){
									winner = i;
									loser = oppos;
								}
								else{
									winner = oppos;
									loser = i;			
								}
								
								board_array[idx]->w = winner;
								board_array[idx]->l = loser;
								
								int winner_idx = table[winner]->player_idx;
								int loser_idx = table[loser]->player_idx;
								board_array[idx]->players_score[winner_idx] += 1;
								
								memset( buffer, '\0', 256 );
								sprintf( buffer, "Congratulates %s, You are a winner!\n", table[winner]->name );
								send( winner, buffer, strlen( buffer ), 0 );
								
								int winner_score = board_array[idx]->players_score[winner_idx];
								int loser_score = board_array[idx]->players_score[loser_idx];
								memset( buffer, '\0', 256 );
								sprintf( buffer, "Your Score: %d Opponent's Score: %d\n", winner_score, loser_score );
								send( winner, buffer, strlen( buffer ), 0 );
								
								memset( buffer, '\0', 256 );
								sprintf( buffer, "%s wins this round...\n", table[winner]->name );
								send( loser, buffer, strlen( buffer ), 0 );

								memset( buffer, '\0', 256 );
								sprintf( buffer, "Your Score: %d Opponent's Score: %d\n",  loser_score, winner_score );
								send( loser, buffer, strlen( buffer ), 0 );

								send( winner, "Play another round? (y/n)\n", 26, 0 );
								send( loser, "Play another round? (y/n)\n", 26, 0 );
								table[i]->next_round = 1;
								table[oppos]->next_round = 1;

								//fprintf( stderr, "fuck!\n" );
								for( int k = 0; k < 5; k++ ){
									if( table[i]->watching[k] != 0 ){
										int watching_fd = table[i]->watching[k];
										fprintf( stderr, "%d", watching_fd );
										table[watching_fd]->watch = 0;
										send( watching_fd, "Game is over...\n", 16, 0 );
										send( watching_fd, "\n", 1, 0 );
										menu( watching_fd );
									}
									if( table[oppos]->watching[k] != 0 ){
										int watching_fd = table[oppos]->watching[k];
										fprintf( stderr, "%d", watching_fd );
										table[watching_fd]->watch = 0;
										send( watching_fd, "Game is over...\n", 16, 0 );
										send( watching_fd, "\n", 1, 0 );
										menu( watching_fd );
									}
								}
								fprintf( stderr, "fuck!\n" );

								memset( table[i]->watching, 0, 5 );
								table[i]->cnt = 0;
								memset( table[oppos]->watching, 0, 5 );
								table[oppos]->cnt = 0;
								//fprintf( stderr, "fuck!\n" );
							}
							else if( board_array[idx]->cnt == 0 ){
								send( i, "The match ended in a tie\n", 25, 0 );
								send( oppos, "The match ended in a tie\n", 25, 0 );

								int my_idx = table[i]->player_idx;
								int oppos_idx = table[oppos]->player_idx;
								int my_score = board_array[idx]->players_score[my_idx];
								int oppos_score = board_array[idx]->players_score[oppos_idx];

								memset( buffer, '\0', 256 );
								sprintf( buffer, "Your Score: %d Opponent's Score: %d\n", my_score, oppos_score );
								send( i, buffer, strlen( buffer ), 0 );

								memset( buffer, '\0', 256 );
								sprintf( buffer, "Your Score: %d Opponent's Score: %d\n", oppos_score, my_score );
								send( oppos, buffer, strlen( buffer ), 0 );

								send( i, "Play another round? (y/n)\n", 26, 0 );
								send( oppos, "Play another round? (y/n)\n", 26, 0 );
								table[i]->next_round = 1;
								table[oppos]->next_round = 1;
								
								for( int k = 0; k < 5; k++ ){
									if( table[i]->watching[k] != 0 ){
										int watching_fd = table[i]->watching[k];
										table[watching_fd]->watch = 0;
										send( watching_fd, "Game is over...\n", 16, 0 );
										send( watching_fd, "\n", 1, 0 );
										menu( watching_fd );
									}
									if( table[oppos]->watching[k] != 0 ){
										int watching_fd = table[oppos]->watching[k];
										table[watching_fd]->watch = 0;
										send( watching_fd, "Game is over...\n", 16, 0 );
										send( watching_fd, "\n", 1, 0 );
										menu( watching_fd );
									}
								}
								
								memset( table[i]->watching, 0, 5 );
								table[i]->cnt = 0;
								memset( table[oppos]->watching, 0, 5 );
								table[oppos]->cnt = 0;
							}
						}
						else if( table[i]->gaming == 1 && table[i]->next_round == 1 ){
							if( !strcmp( read, "y" ) ){
								table[i]->verify = 1;
								send( i, "Wait for opponent to acknowledge\n", 33, 0 );
							}
							else if( !strcmp( read, "n" ) ){
								table[i]->verify = 0;
								send( i, "Wait for opponent to acknowledge\n", 33, 0 );
							}

							int oppos = table[i]->oppos;
							if( table[i]->verify == 1 && table[oppos]->verify == 1 ){
								int idx = table[i]->board_idx;
								for( int j = 0; j < 3; j++ )
									for( int k = 0; k < 3; k++ )
										board_array[idx]->play_board[j][k] = 3 * j + k + 1 + '0';
									
								char buffer[256] = {0}, temp[32] = {0};
								strcat( buffer, " _________________\n" );
								strcat( buffer, "|     |     |     | \n" );
								sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[idx]->play_board[0][0], board_array[idx]->play_board[0][1], board_array[idx]->play_board[0][2] );
								strcat( buffer, temp );
									
								strcat( buffer, "|_____|_____|_____|\n" );
								strcat( buffer, "|     |     |     | \n" );
								sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[idx]->play_board[1][0], board_array[idx]->play_board[1][1], board_array[idx]->play_board[1][2] );
								strcat( buffer, temp );
									
								strcat( buffer, "|_____|_____|_____|\n" );
								strcat( buffer, "|     |     |     | \n" );
								sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[idx]->play_board[2][0], board_array[idx]->play_board[2][1], board_array[idx]->play_board[2][2] );
								strcat( buffer, temp );
									
								strcat( buffer, "|_____|_____|_____|\n" );

								send( i, buffer, strlen( buffer ), 0 );
								send( oppos, buffer, strlen( buffer ), 0 );
								
								int winner = board_array[idx]->w;
								int loser = board_array[idx]->l;
								board_array[idx]->turn = loser;
								
								memset( buffer, '\0', 256 );
								sprintf( buffer, "Waiting for %s...\n", table[loser]->name );
								send( winner, buffer, strlen( buffer ), 0 );
									
								memset( buffer, '\0', 256 );
								sprintf( buffer, "%s, please enter the number of the square where you want to place your %c:\n", table[loser]->name, ( table[loser]->player_idx == 1 )?'X':'O' );
								send( loser, buffer, strlen( buffer ), 0 );

								board_array[idx]->cnt = 9;
								
								table[i]->verify = -1;
								table[i]->next_round = 0;
								
								table[oppos]->verify = -1;
								table[oppos]->next_round = 0;
							}
							else if( table[i]->verify != -1 && table[oppos]->verify != -1 ){
								fprintf( stderr, "Cancel game\n" );
								if( table[i]->verify == 1 ){
									send( i, "Opponent has declined...\n", 25, 0 );
								}
								else if( table[oppos]->verify == 1 ){
									send( oppos, "Opponent has declined...\n", 25, 0 );
								}
								
								int idx = table[i]->board_idx;
								int my_idx = table[i]->player_idx, oppos_idx = table[oppos]->player_idx;
								table[i]->win += board_array[idx]->players_score[my_idx];
								table[i]->lose += board_array[idx]->players_score[oppos_idx];
								table[oppos]->win += board_array[idx]->players_score[oppos_idx];
								table[oppos]->lose += board_array[idx]->players_score[my_idx];
								
								fprintf( stderr, "Free the %d board\n", idx );
								free( board_array[idx] );
								board_array[idx] = NULL;
								
								init( i );
								init( oppos );
								send( i, "\n", 1, 0 );	
								menu( i );
								send( oppos, "\n", 1, 0 );	
								menu( oppos );
							}
						}
						else if( table[i]->watch != 0 ){
							if( !strcmp( read, "quit" ) ){
								int watched_fd = table[i]->watch; 
								for( int k = 0; k < 5; k++ ){
									if( table[watched_fd]->watching[k] == i ){
										table[watched_fd]->watching[k] = 0;
										send( i, "\n", 1, 0 );
										menu( i );
									}
								}
								table[i]->watch = 0;
							}
						}
						else{
							if( !strcmp( read, "list" ) ){
								char buffer[100];
								memset( buffer, '\0', sizeof( buffer ) );
								strcpy( buffer, "Who Is Online:\n");

								int have = 0;
								for( int k = 0; k < 32; k++ ){
									if( table[k] != NULL ){
										if( !strcmp( table[k]->name, table[i]->name ) )
											continue;
											
										strcat( buffer, table[k]->name );
										if( table[k]->gaming == 1 )
											strcat( buffer, " (Playing)" );
										else if( table[k]->gaming == 0 )
											strcat( buffer, " (Idle)" );
											
										int len = strlen( buffer );
										buffer[len] = '\n';
										have = 1;
									}
								}
								
								if( have ){
									send( i, "\n", 1, 0 );
									send( i, buffer, strlen( buffer ), 0 );
								}
								else{
									send( i, "\n", 1, 0 );
									send( i, "None...\n", 8, 0 );
								}
								send( i, "\n", 1, 0 );
								menu( i );
							}
							
							else if( !strncmp( read, "invite", 6 ) ){
								char* invited_user = strstr( read, " " ) + 1;
								fprintf( stderr, "%s invite %s to play\n", table[i]->name, invited_user );

								int invited_fd;
								h = hash( invited_user ) % 10000;
								if( list[h] != NULL )
									invited_fd = list[h]->id;
								else{
									char buffer[64];
									send( i, "\n", 1, 0 );
									sprintf( buffer, "%s not found...\n", invited_user );
									send( i, buffer, strlen( buffer ), 0 );
									send( i, "\n", 1, 0 );
									menu( i );
									continue;
								}
														
								if( table[invited_fd]->gaming == 1 ){
									char buffer[64];
									send( i, "\n", 1, 0 );
									sprintf( buffer, "%s is playing now. Wait for a second\n", invited_user );
									send( i, buffer, strlen( buffer ), 0 );
									send( i, "\n", 1, 0 );
									menu( i );
									continue;
								}

								table[i]->oppos = invited_fd;
								
								table[invited_fd]->invited = 1;
								table[invited_fd]->oppos = i;
								
								char buffer[64];
								send( invited_fd, "\n", 1, 0 );
								sprintf( buffer, "%s has challenged you. Accept challenge? (y/n)\n", table[i]->name );
								send( invited_fd, buffer, strlen( buffer ), 0 );

								send( i, "\n", 1, 0 );
								send( i, "Wait for opponent to acknowledge...\n", 36, 0 );
							}

							else if( !strcmp( read, "logout" ) ){
								fprintf( stderr, "Account: %s logged out\n", table[i]->name );
								
								h = hash( table[i]->name ) % 10000;
								list[h]->online = 0;
								list[h]->id = -1;

								send( i, "\n", 1, 0 );
								send( i, "Goodbye~\n", 9, 0 );
								drop( i, &master );
								continue;
							}

							else if( table[i]->invited == 1 ){
								if( !strcmp( read, "y" ) ){
									table[i]->gaming = 1;
									table[i]->invited = 0;
									table[i]->board_idx = i;
									table[i]->player_idx = 1;
									
									int oppos = table[i]->oppos;
									table[oppos]->gaming = 1;
									table[oppos]->board_idx = i;
									table[oppos]->player_idx = 0;

									board_array[i] = (struct board_items*)malloc( sizeof( struct board_items ) );
									board_array[i]->turn = oppos;
									board_array[i]->cnt = 9;
									memset( board_array[i]->players_score, 0, 2 );

									for( int j = 0; j < 3; j++ )
										for( int k = 0; k < 3; k++ )
											board_array[i]->play_board[j][k] = 3 * j + k + 1 + '0';
									
									char buffer[256] = {0}, temp[32] = {0};
									strcat( buffer, " _________________\n" );
									strcat( buffer, "|     |     |     | \n" );
									sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[i]->play_board[0][0], board_array[i]->play_board[0][1], board_array[i]->play_board[0][2] );
									strcat( buffer, temp );
									
									strcat( buffer, "|_____|_____|_____|\n" );
									strcat( buffer, "|     |     |     | \n" );
									sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[i]->play_board[1][0], board_array[i]->play_board[1][1], board_array[i]->play_board[1][2] );
									strcat( buffer, temp );
									
									strcat( buffer, "|_____|_____|_____|\n" );
									strcat( buffer, "|     |     |     | \n" );
									sprintf( temp, "|  %c  |  %c  |  %c  |\n", board_array[i]->play_board[2][0], board_array[i]->play_board[2][1], board_array[i]->play_board[2][2] );
									strcat( buffer, temp );
									
									strcat( buffer, "|_____|_____|_____|\n" );

									send( i, buffer, strlen( buffer ), 0 );
									send( oppos, buffer, strlen( buffer ), 0 );

									memset( buffer, '\0', 256 );
									sprintf( buffer, "Waiting for %s...\n", table[oppos]->name );
									send( i, buffer, strlen( buffer ), 0 );
									
									memset( buffer, '\0', 256 );
									sprintf( buffer, "%s, please enter the number of the square where you want to place your %c:\n", table[oppos]->name, ( table[oppos]->player_idx == 1 )?'X':'O' );
									send( oppos, buffer, strlen( buffer ), 0 );
								}
								else if( !strcmp( read, "n" ) ){
									int oppos = table[i]->oppos;
									table[oppos]->oppos = -1;
									table[i]->invited = 0;
									table[i]->oppos = -1;

									char buffer[64] = {0};
									send( i, "\n", 1, 0 );
									sprintf( buffer, "%s has rejected your challenge\n", table[i]->name );
									send( oppos, buffer, strlen( buffer ), 0 );
									send( i, "\n", 1, 0 );
									menu( i );
								}
								else
									continue;
							}
							else if( !strncmp( read, "info", 4 ) ){
								char* info_user = strstr( read, " " ) + 1;
								fprintf( stderr, "%s infos %s\n", table[i]->name, info_user );
								
								int info_fd;
								h = hash( info_user ) % 10000;
								if( list[h] != NULL ){
									char buffer[64];
									info_fd = list[h]->id;
									if( table[info_fd]->win + table[info_fd]->lose != 0 ){
										float r = (float)table[info_fd]->win / ( (float)table[info_fd]->win + (float)table[info_fd]->lose ) * 100.0;
										sprintf( buffer, "Win = %d Lose = %d Win rate = %f%%\n", table[info_fd]->win, table[info_fd]->lose, r );
										send( i, "\n", 1, 0 );
										send( i, buffer, strlen( buffer ), 0 );
									}
									else{
										send( i, "\n", 1, 0 );
										send( i, "This player has not have records yet...\n", 40, 0 );		
									}

									send( i, "\n", 1, 0 );
									menu( i );	
								}
								else{
									char buffer[64];
									send( i, "\n", 1, 0 );
									sprintf( buffer, "%s not found...\n", info_user );
									send( i, buffer, strlen( buffer ), 0 );
									send( i, "\n", 1, 0 );
									menu( i );
									continue;
								}

								
							}
							else if( !strncmp( read, "watch", 5 ) ){
								char* watched_user = strstr( read, " " ) + 1;
								fprintf( stderr, "%s is watching %s\n", table[i]->name, watched_user );

								int watched_fd;
								h = hash( watched_user ) % 10000;
								if( list[h] != NULL ){
									watched_fd = list[h]->id;
									if( table[watched_fd]->gaming == 1 ){
										char buffer[64];
										int cnt = table[watched_fd]->cnt;
										table[i]->watch = watched_fd;
										table[watched_fd]->watching[cnt] = i;
										table[watched_fd]->cnt++;
									
										int oppos = table[watched_fd]->oppos;
									
										sprintf( buffer, "You are watching %s(%c) vs %s(%c)\n", table[watched_fd]->name
										, ( table[watched_fd]->player_idx == 1 )?'X':'O'
										, table[oppos]->name
										, ( table[oppos]->player_idx == 1 )?'X':'O' );

										send( i, "\n", 1, 0 );
										send( i, buffer, strlen( buffer ), 0 );
										send( i, "You can key in \'quit\' to stop watching\n", 39, 0 );
									}
									else{
										send( i, "\n", 1, 0 );
										send( i, "This player is not in game now...\n", 34, 0 );
										send( i, "\n", 1, 0 );
										menu( i );
									}	
								}
								else{
									char buffer[64];
									send( i, "\n", 1, 0 );
									sprintf( buffer, "%s not found...\n", watched_user );
									send( i, buffer, strlen( buffer ), 0 );
									send( i, "\n", 1, 0 );
									menu( i );
									continue;
								}
							}
							else if( !strncmp( read, "send", 4 ) ){
								char* q = strstr( read, " " );
								if( q == NULL ){
									send( i, "\n", 1, 0 );
									send( i, "Usage: send {username} {message}\n", 33, 0 );
									send( i, "\n", 1, 0 );
									menu( i );
									continue;
								}
								*q = '\0';
								char* sended_user = q + 1;

								q = strstr( sended_user, " " );
								if( q == NULL ){
									send( i, "\n", 1, 0 );
									send( i, "Usage: send {username} {message}\n", 33, 0 );
									send( i, "\n", 1, 0 );
									continue;
								}
								*q = '\0';
								char* content = q + 1;
								
								fprintf( stderr, "%s sends content to %s\n", table[i]->name, sended_user );

								int sended_fd;
								h = hash( sended_user ) % 10000;
								if( list[h] != NULL ){
									if( list[h]->online == 1 ){
										sended_fd = list[h]->id;
										char buffer[64];
										sprintf( buffer, "%s: %s\n", table[i]->name, content );
										send( sended_fd, "\n", 1, 0 );
										send( sended_fd, buffer, strlen( buffer ), 0 );
										send( sended_fd, "\n", 1, 0 );
										menu( sended_fd );
									
										send( i, "\n", 1, 0 );
										menu( i );
									}
									else{
										send( i, "\n", 1, 0 );
										send( i, "This player is not online now...\n", 34, 0 );		
									}	
								}
								else{
									char buffer[64];
									send( i, "\n", 1, 0 );
									sprintf( buffer, "%s not found...\n", sended_user );
									send( i, buffer, strlen( buffer ), 0 );
									send( i, "\n", 1, 0 );
									menu( i );
									continue;
								}
							
							}
							else{
								send( i, "\n", 1, 0 );
								send( i, "No such options!\n", 17, 0 );
								send( i, "\n", 1, 0 );
								menu( i );
							}
						}
					}
				}
			}
		}
	}

	printf("Closing listening socket...\n");
	close( socket_listen );

	printf("Finished.\n");

	return 0;
}

unsigned long hash( unsigned char* str ){
	unsigned long hash = 5381;
	int c;

	while( ( c = *str++ ) )
		hash = ( ( hash << 5 ) + hash ) + c;

	return hash;
}

char *str2md5( const char *str, int length ){
    int n;
    MD5_CTX c;
    unsigned char digest[16];
    char *out = (char*)malloc(33);

    MD5_Init( &c );

    while( length > 0 ){
        if( length > 512 ){
            MD5_Update( &c, str, 512 );
        } 
		else{
            MD5_Update( &c, str, length );
        }
        length -= 512;
        str += 512;
    }

    MD5_Final( digest, &c );

    for( n = 0; n < 16; ++n ){
        snprintf( &(out[n*2]), 16*2, "%02x", (unsigned int)digest[n] );
    }

    return out;
}

int check( const char board[3][3] ){
	for( int i = 0; i < 3; i++ ){
		if( board[i][0] == board[i][1] && board[i][1] == board[i][2] ){
			if( board[i][0] == 'X' )
				return 1;
			else
				return 0;
		}
		if( board[0][i] == board[1][i] && board[1][i] == board[2][i] ){
			if( board[0][i] == 'X' )
				return 1;
			else
				return 0;
		}
	}

	if( board[0][0] == board[1][1] && board[1][1] == board[2][2] ){
		if( board[0][0] == 'X' )
			return 1;
		else
			return 0;
	}
	if( board[0][2] == board[1][1] && board[1][1] == board[2][0] ){
		if( board[2][0] == 'X' )
			return 1;
		else
			return 0;
	}

	return 2;
}

void menu( int fd ){
	char buffer[256] = {0};
	strcat( buffer, "Menu:\n" );
	strcat( buffer, "(1) list --To know who is online\n" );
	strcat( buffer, "(2) invite {username} --Invite someone to play with you\n" );
	strcat( buffer, "(3) info {username} --Player\'s infomation\n" );
	strcat( buffer, "(4) watch {username} --Watch other\'s game\n" );
	strcat( buffer, "(5) send {username} {message} --Send a message to other player\n" );
	strcat( buffer, "(6) logout\n" );

	send( fd, buffer, strlen( buffer ), 0 );
}

void drop( int fd, fd_set* m ){
	free( table[fd]->name );
	table[fd]->name = NULL;
	free( table[fd] );
	table[fd] = NULL;
	FD_CLR( fd, m );
	close( fd );
}

void init( int fd ){
	table[fd]->gaming = 0;
	table[fd]->oppos = -1;
	table[fd]->board_idx = -1;
	table[fd]->player_idx = -1;
	table[fd]->verify = -1;
	table[fd]->next_round = 0;
	table[fd]->cnt = 0;
	table[fd]->watch = 0;
	memset( table[fd]->watching, 0, sizeof( table[fd]->watching ) );
}

void logo( int fd ){
	char buffer[1024] = {0};
	strcat( buffer, "--------------------------------------------------------------------------\n" );
	strcat( buffer, "| # # #   # # #    # # #   # # #    #      # # #  # # #   # # #   # # #  |\n" );
	strcat( buffer, "|   #       #      #   #     #     #  #    #   #    #     #   #   #      |\n" );
	strcat( buffer, "|   #       #      #         #    #    #   #        #     #   #   # # #  |\n" );
	strcat( buffer, "|   #       #      #   #     #   # #  # #  #   #    #     #   #   #      |\n" );
	strcat( buffer, "|   #     # # #    # # #     #  #        # # # #    #     # # #   # # #  |\n" );
	strcat( buffer, "--------------------------------------------------------------------------\n" );

	send( fd, buffer, strlen( buffer ), 0 );
}

void sighandler_ctrlc(){
	fprintf( stderr, "Ctrl-c\n" );
	int time = 10, fd;
	unsigned long h;
	while( time != 0 ){
		for( int i = 0; i <= max_socket; i++ )
			if( table[i] != NULL ){
				h = hash( table[i]->name ) % 10000;
				fd = list[h]->id;
				char buffer[64] = {0};
				sprintf( buffer, "Sorry... Server will close in %d seconds...\n", time );
				send( fd, buffer, strlen( buffer ), 0 );	
			}
		time -= 1;
		sleep(1);
	}
	for( int i = 0; i <= max_socket; i++ )
		if( table[i] != NULL ){
			h = hash( table[i]->name ) % 10000;					
			fd = list[h]->id;
			char* buffer = "Bye~\n";
			send( fd, buffer, strlen( buffer ), 0 );	
		}
	
	printf("Closing listening socket...\n");
	close( socket_listen );

	printf("Finished.\n");
	exit(0);
}

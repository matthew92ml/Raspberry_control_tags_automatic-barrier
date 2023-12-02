#include "libssh2_config.h"
#include <libssh2.h>
#include <libpq-fe.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include <iostream>
#include <wiringPi.h>
#include <pthread.h>

using namespace std;

// RELAY Pin - wiringPi pin 0 is BCM_GPIO 17.

#define RELAY     0
void apriCancello();
void chiudiCancello();
int codiceCorretto(string codice);
void *sshTunnel(void *args);

//collegamento scheda rele'
/*
 RASP LATO SD-------------
 
		xx----> 5V
	    xx
		xx----> GND
		xx
		xx
 OUT<---xx
		xx
		xx
		xx
		xx
		xx
		xx
		xx

---------------------------
*/

int main (void)
{
	pthread_t threadSSH;
	int retSSH;
	
	string *server_ip = "156.54.75.179";
	string codice;
	
	cout << "Ecofil gestione cancello Isola Ecologica" << endl ;

	wiringPiSetup () ;
	pinMode (RELAY, OUTPUT) ;


	retSSH = pthread_create(&threadSSH,NULL,sshTunnel,(void*)server_ip.c_str());
	if(retSSH){
		cerr << "ERROR CODE: " << retSSH << endl;
	}
	while(1)
	{
		getline(cin,codice,'\n');
		cout <<  "Codice ricevuto: " << codice << endl ;
		
		
		if(codiceCorretto(codice)>0) 
			apriCancello();
		else{
			if(codice == "OPEN") apriCancello();
			else if(codice == "CLOSE") chiudiCancello();
			else if(codice == "STOP") break;
		}
	}
	cout << "Fine" << endl ;
	return 0 ;
}

void *sshTunnel(void *args){
	string username = "grduser";
	string password = "ecofil2014";

	string local_listenip = "127.0.0.1";
	unsigned int local_listenport = 54320;

	const char *remote_desthost = "localhost";
	unsigned int remote_destport = 5432;
	
	char *server_ip = (char*) args;
	
	int rc, sock = -1, listensock = -1, forwardsock = -1, i;
    struct sockaddr_in sin;
    socklen_t sinlen;

    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel = NULL;
    string shost;
    unsigned int sport;
    fd_set fds;
    struct timeval tv;
    ssize_t len, wr;
    char buf[16384];
    
	int ret = 0;
	int sockopt;

restart:
    rc = libssh2_init (0);

    if (rc != 0) {
        cerr << "libssh2 initialization failed: " << rc << endl;
        ret = 1;
        return &ret;
    }

    /* Create a session instance */
    session = libssh2_session_init();

    if(!session) {
        cerr <<  "Could not initialize SSH session!"<< endl;
        ret = -1;
        return &ret;
    }

    /* Connect to SSH server */
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    sin.sin_family = AF_INET;
    if (INADDR_NONE == (sin.sin_addr.s_addr = inet_addr(server_ip.c_str()))) {
        cerr << "inet_addr" << endl;
        ret = -1;
        return &ret;
    }
    sin.sin_port = htons(22);
    if (connect(sock, (struct sockaddr*)(&sin),
                sizeof(struct sockaddr_in)) != 0) {
        cerr << "failed to connect!"<< endl;
        sleep(5);
        goto shutdown;
    }


    rc = libssh2_session_handshake(session, sock);

    if(rc) {
        cerr << "Error when starting up SSH session: "<< rc << endl;
        ret = -1;
        return &ret;
    }

    if (libssh2_userauth_password(session, username.c_str(), password.c_str())) {
        cerr << "Authentication by password failed"<< endl;
        goto shutdown;
    }

    listensock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(local_listenport);
    if (INADDR_NONE == (sin.sin_addr.s_addr = inet_addr(local_listenip.c_str()))) {
        cerr << "error: inet_addr" << endl;
        goto shutdown;
    }
    sockopt = 1;
    setsockopt(listensock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
    sinlen=sizeof(sin);
    if (-1 == bind(listensock, (struct sockaddr *)&sin, sinlen)) {
        cerr << "error: bind" << endl;
        goto shutdown;
    }
    if (-1 == listen(listensock, 2)) {
        cerr << "error: listen" << endl;
        goto shutdown;
    }

    cout << "Waiting for TCP connection on "<< sin.sin_addr<<":"<<sin.sin_port<<"..."<< endl;
        //inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

    forwardsock = accept(listensock, (struct sockaddr *)&sin, &sinlen);
    if (-1 == forwardsock) {
        cerr << "error: accept" << endl;
        goto shutdown;
    }

    shost = inet_ntoa(sin.sin_addr);
    sport = ntohs(sin.sin_port);

    cout << "Forwarding connection" << endl;

    channel = libssh2_channel_direct_tcpip_ex(session, remote_desthost,remote_destport, shost.c_str(), sport);
    
    if (!channel) {
        cerr << "Could not open the direct-tcpip channel" << endl;
        goto shutdown;
    }

    /* Must use non-blocking IO hereafter due to the current libssh2 API */
    libssh2_session_set_blocking(session, 0);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(forwardsock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        rc = select(forwardsock + 1, &fds, NULL, NULL, &tv);
        if (-1 == rc) {
            cerr << "error:select"<<endl;
            goto shutdown;
        }
        if (rc && FD_ISSET(forwardsock, &fds)) {
            len = recv(forwardsock, buf, sizeof(buf), 0);
            if (len < 0) {
            	cerr << "error:read"<<endl;
                goto shutdown;
            } else if (0 == len) {
                cerr << "The client is disconnected!" << endl;
                goto shutdown;
            }
            wr = 0;
            do {
                i = libssh2_channel_write(channel, buf, len);

                if (i < 0) {
                    cerr << "libssh2_channel_write: " << i << endl;
                    goto shutdown;
                }
                wr += i;
            } while(i > 0 && wr < len);
        }
        while (1) {
            len = libssh2_channel_read(channel, buf, sizeof(buf));

            if (LIBSSH2_ERROR_EAGAIN == len)
                break;
            else if (len < 0) {
                cerr << "libssh2_channel_read: " << (int)len << endl;
                goto shutdown;
            }
            wr = 0;
            while (wr < len) {
                i = send(forwardsock, buf + wr, len - wr, 0);
                if (i <= 0) {
                    cerr << "write" << endl;
                    goto shutdown;
                }
                wr += i;
            }
            if (libssh2_channel_eof(channel)) {

                cerr << "The server is disconnected!" << endl;
                goto shutdown;
            }
        }
    }

shutdown:

    close(forwardsock);
    close(listensock);

    if (channel)
        libssh2_channel_free(channel);

    libssh2_session_disconnect(session, "Client disconnecting normally");

    libssh2_session_free(session);
    close(sock);
    libssh2_exit();

    goto restart;
    return &ret;
}

void apriCancello(){
	digitalWrite (RELAY, LOW) ;  // On
	delay (500) ; 
}

void chiudiCancello(){
	digitalWrite (RELAY, HIGH) ;   // Off
    delay (500) ;
}


int codiceCorretto(string codice){
    string conninfo;
    PGconn     *conn;
    PGresult   *res;
	int ntuples = 0;
	string querystr;
	
	
    conninfo = "host = localhost port = 54320 user = postgres password = grdpostgres dbname = grddb";
    conn = PQconnectdb(conninfo.c_str());

    /* Check to see that the backend connection was successfully made */
    if (PQstatus(conn) != CONNECTION_OK)
    {
        printf("Connection to database failed: %s",PQerrorMessage(conn));
        PQfinish(conn);
        return -1;
    }
	else{
		printf("Connection OK!\n");
	}
   
    /* Start a transaction block */
    res = PQexec(conn, "BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        printf("BEGIN command failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return -2;
    }
	else{
		printf("BEGIN command OK!\n");
	}
    PQclear(res);

    querystr = std::string("select id from infonuclei WHERE codicetag = '") + codice + "'";
    res = PQexec(conn, querystr.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        printf("Psql Error: %s\n", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return -3;
    }
    ntuples = PQntuples(res);
    PQclear(res);
    
    querystr = std::string("select id from infooperatori WHERE codicetag = '") + codice + "'";
    res = PQexec(conn, querystr.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        printf("Psql Error: %s\n", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return -3;
    }
    ntuples += PQntuples(res);
    PQclear(res);
    
    querystr = std::string("select id from infoamministratori WHERE codicetag = '") + codice + "'";
    res = PQexec(conn, querystr.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        printf("Psql Error: %s\n", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return -3;
    }
    ntuples += PQntuples(res);
    PQclear(res);
    
    printf("risultato: %d\n",ntuples);
    
    /* end the transaction */
    res = PQexec(conn, "END");
    PQclear(res);

    /* close the connection to the database and cleanup */
    PQfinish(conn);

	
	return ntuples;

}

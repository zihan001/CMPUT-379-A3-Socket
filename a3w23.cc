// Program based on sockMsg.cc provided in eclass

#define _REENTRANT 

#include <unistd.h>

#include <iostream>
#include <string>
#include <cstdio> 
#include <cstdlib> 
#include <cassert> 
#include <cmath> 
#include <ctime> 
#include <cstring> 
#include <cstdarg>
#include <chrono>

#include <sys/types.h> 
#include <sys/stat.h> 
#include <sys/wait.h> 
#include <sys/time.h> 
#include <sys/times.h> 
#include <fcntl.h> 
#include <errno.h>

#include <netinet/in.h> 
#include <sys/socket.h> 
#include <netdb.h>

#include <poll.h>

#include <string> 
#include <fstream>
#include <vector> 
using namespace std;

// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
#define MAXLINE 256 
#define MAXWORD 32

#define NCLIENT 3 // client limit 

#define NITER 10    // number of iterations per client
#define MSG_KINDS 8
// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−

// data structures needed to make a frame to send message.
typedef enum { HELLO, PUT, GET, DELETE, GTIME, QUIT, OK, DONE, ERROR, TIME } KIND; // Message kinds 
char KINDNAME[][MAXWORD]= { "HELLO", "PUT", "GET", "DELETE", "GTIME", "QUIT", "OK", "DONE", "ERROR", "TIME" };

typedef struct { int client; } MSG_HELLO;
typedef struct {
    int client;
    char file[MAXLINE];
    char lines[3][MAXLINE];
    int lineNo;
} MSG_PUT;
typedef struct {
    int client;
    char file[MAXLINE];
} MSG_GET, MSG_DLT;
typedef struct {
    int client;
    char empty[1];
} MSG_GTIME;
typedef struct {
    int client;
    char error[MAXLINE];
} MSG_ERROR;
typedef struct {
    int client;
    clock_t time;
} MSG_TIME;
typedef struct { int d[3]; } MSG_INT;

typedef union { MSG_HELLO mHello; MSG_PUT mPut; MSG_GET mGet; MSG_DLT mDlt; MSG_INT mInt; MSG_GTIME mGT; MSG_ERROR mError; MSG_TIME mTime; } MSG;
typedef struct { KIND kind; MSG msg; } FRAME;
#define MAXBUF sizeof(FRAME)
typedef struct sockaddr SA;

// Figure 8.31 of [APUE 3/E]
static void pr_times(clock_t real, struct tms *tmsstart, struct tms *tmsend)
{
    static long clktck = 0;
    if (clktck == 0)    /* fetch clock ticks per second first time */
        clktck = sysconf(_SC_CLK_TCK);
    printf("  real:  %7.2f\n", real / (double) clktck);
    printf("  user:  %7.2f\n",
      (tmsend->tms_utime - tmsstart->tms_utime) / (double) clktck);
    printf("  sys:   %7.2f\n",
      (tmsend->tms_stime - tmsstart->tms_stime) / (double) clktck);
}

// delete element of a list and bring later elements back one index.
void deleteElement(MSG_PUT arr[], int n, char string[])
{
    // Search x in array
    int i;
    for (i=0; i<n; i++)
        if (!strcmp(arr[i].file, string)) {
            break;
        }
    
    // If x found in array
    if (i < n)
    {
        // reduce size of array and move all
        // elements on space ahead
        n = n - 1;
        for (int j=i; j<n; j++) {
            arr[j].client = arr[j+1].client;
            strcpy(arr[j].file, arr[j+1].file);
            for (int k = 0; i < arr[j+1].lineNo; k++) {
                strcpy(arr[j].lines[k], arr[j+1].lines[k]);
            }
        }
    }
}
// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
// The WARNING and FATAL functions are due to the authors of
// the AWK Programming Language.
void FATAL (const char *fmt, ... )
{
    va_list ap;
    fflush (stdout);
    va_start (ap, fmt); vfprintf (stderr, fmt, ap); va_end(ap);
    fflush (NULL);
    exit(1);
}

void WARNING (const char *fmt, ... )
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
}
// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−

// make a hello packet when client connects to send to server
MSG composeHELLO (int idNo)
{
    MSG msg;

    memset( (char *) &msg, 0, sizeof(msg) ); 
    msg.mHello.client= idNo;
    return msg;
}

// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
// composeMINT − compose a message of 3 integers: a, b, and c. 
MSG composeMINT (int a, int b, int c)
{
    MSG msg;

    memset( (char *) &msg, 0, sizeof(msg) );
    msg.mInt.d[0]= a; msg.mInt.d[1]= b; msg.mInt.d[2]= c;
    return msg;
}
// ------------------------------
// send a frame which is a type of packet which has the kind of packet it is along with the message body to server or client
int sendFrame (int fd, KIND kind, MSG *msg)
{
    int    len;
    FRAME  frame;

    assert (fd >= 0);
    memset( (char *) &frame, 0, sizeof(frame) );
    frame.kind= kind;
    frame.msg=  *msg;

    len= write (fd, (char *) &frame, sizeof(frame));
    if (len == 0) WARNING ("sendFrame: sent frame has zero length. \n");
    else if (len != sizeof(frame))
        WARNING ("sendFrame: sent frame has length= %d (expected= %d)\n",
	          len, sizeof(frame));
    return len;		  
}
// ------------------------------
// recieve a packet of information from server or client
int rcvFrame (int fd, FRAME *framep)
{
    int    len; 
    FRAME  frame;

    assert (fd >= 0);
    memset( (char *) &frame, 0, sizeof(frame) );

    len= read (fd, (char *) &frame, sizeof(frame));
    *framep= frame;
    
    if (len == 0) WARNING ("rcvFrame: received frame has zero length \n");
    else if (len != sizeof(frame))
        WARNING ("rcvFrame: received frame has length= %d (expected= %d)\n",
		  len, sizeof(frame));
    return len;
}
// print a packet of information
void printFrame (const char *prefix, FRAME *frame, int idNo)
{
    MSG  msg= frame->msg;
    
    switch (frame->kind) {	
        case HELLO:
            printf("%s (src= %d) (HELLO, idNumber= %d)", prefix, idNo, idNo);
            break;
        case GET:
            for (int i = 0; i < frame->msg.mPut.lineNo; i++) {
                printf("[%d]: %s\n", i, frame->msg.mPut.lines[i]);
            }
            break;
        case OK: 
            printf("%s (src= 0) %s", prefix, KINDNAME[frame->kind]);
            break;
        case DONE:
            break;
        default:
            WARNING ("Unknown frame type (%d)\n", frame->kind);
        break;
    }
    printf("\n");
}
// ------------------------------
// testDone: return 0 if all done flags are -1 (i.e., no client has started),
//           or if at least one done flag is zero (i.e., at least one client
// 	     is still working)

int testDone (int done[], int nClient)
{
    int  i, sum= 0;

    if (nClient == 0) return 0;
    
    for (i= 1; i <= nClient; i++) sum += done[i];
    if (sum == -nClient) return 0;

    for (i= 1; i <= nClient; i++) { if (done[i] == 0) return 0; }
    return 1;
}    


// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
// client connects to the server
int clientConnect (const char *serverName, int portNo)
{
    int sfd;
    struct sockaddr_in server;
    struct hostent *hp;     // host entity

    // lookup the specified host
    //
    hp= gethostbyname(serverName);
    if (hp == (struct hostent *) NULL)
        FATAL("clientConnect: failed gethostbyname '%s'\n", serverName);
    
    // put the host’s address, and type into a socket structure
    //
    memset ((char *) &server, 0, sizeof server);
    memcpy ((char *) &server.sin_addr, hp->h_addr, hp->h_length);
    server.sin_family= AF_INET;
    server.sin_port= htons(portNo);

    // create a socket, and initiate a connection
    //
    if ( (sfd= socket(AF_INET, SOCK_STREAM, 0)) < 0)
        FATAL ("clientConnect: failed to create a socket \n");
    
    if (connect(sfd, (SA *) &server, sizeof(server)) < 0)
        FATAL ("clientConnect: failed to connect \n");

    return sfd;
}
// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
int serverListen (int portNo, int nClient)
{
    int sfd;
    struct sockaddr_in sin;

    memset ((char *) &sin, 0, sizeof(sin));

    // create a managing socket
    //
    if ( (sfd= socket (AF_INET, SOCK_STREAM, 0)) < 0)
        FATAL ("serverListen: failed to create a socket \n");

    // bind the managing socket to a name
    //
    sin.sin_family= AF_INET;
    sin.sin_addr.s_addr= htonl(INADDR_ANY);
    sin.sin_port= htons(portNo);

    if (bind (sfd, (SA *) &sin, sizeof(sin)) < 0)
        FATAL ("serverListen: bind failed \n");

    // indicate how many connection requests can be queued

    listen (sfd, nClient);
    return sfd;
}

// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
void do_client (int idNo, char* inputFile, char *serverArg, int portNo)
{
    int i, len, sfd;
    char serverName[MAXWORD];
    char str[MAXLINE];
    char *token[4];
    char WSPACE[]= "\n \t";
    MSG msg;
    FRAME frame;
    struct timespec delay;
    clock_t start, end;
    struct tms  tmsstart, tmsend;

    // connect to server
    strcpy (serverName, serverArg);
    sfd= clientConnect (serverName, portNo);
    printf ("main: do_client (idNumber= %d, inputFile= '%s')\n\t(server= '%s', port= %d)\n\n",
        idNo, inputFile, serverArg, portNo);
    start = times(&tmsstart);

    delay.tv_sec= 0;
    delay.tv_nsec= 900E+6; // 900 millsec (this field can’t be 1 second)

    // send hello packet when connected to server
    msg= composeHELLO(idNo);
    len= sendFrame (sfd, HELLO, &msg);
    printf("Transmitted (src= %d) (HELLO, idNumber= %d)\n", idNo, idNo);
    len= rcvFrame(sfd, &frame); printFrame("Received", &frame, 0);
    printf("\n");

    // open inputFile to read from it
    FILE* fptr;
    fptr = fopen(inputFile, "r");
    if (fptr==NULL) {
        printf("file faied to open");
        exit(-1);
    }
    // main loop to read text lines
    while (fgets(str, 1000, fptr) != NULL) {
        int i = 0;
        //if ((str[0] != '#') && (str[0] != '\n') && str[0] != '}') {
        if (str[0]==(idNo+'0')) {
            // tokenizes the input line
            token[i] = strtok(str, WSPACE);
            while ( token[i] != NULL ) {
                i++;
                token[i] = strtok(NULL, WSPACE);
            }
            // if "put" send a PUT package containing client id, object name, no. of lines in object, content of the object
            // to the server. Wait to recieve an ERROR package if object already exists or an OK package if object was
            // stored successfully.
            if (!strcmp(token[1], "put")) {
                MSG put_msg;
                memset( (char *) &put_msg, 0, sizeof(put_msg) ); 
                put_msg.mPut.client = idNo;
                strcpy(put_msg.mPut.file, token[2]);
                fgets(str, 1000, fptr);
                fgets(str, 1000, fptr);
                if (str[0] != '}') {
                    str[strcspn(str, "\n")]=0;
                    strcpy(put_msg.mPut.lines[0], str);
                    put_msg.mPut.lineNo = 1;
                    fgets(str, 1000, fptr);
                    if (str[0] != '}') {
                        str[strcspn(str, "\n")]=0;
                        strcpy(put_msg.mPut.lines[1], str);
                        put_msg.mPut.lineNo = 2;
                        fgets(str, 1000, fptr);
                        if (str[0] != '}') {
                            str[strcspn(str, "\n")]=0;
                            strcpy(put_msg.mPut.lines[2], str);
                            put_msg.mPut.lineNo = 3;
                        }
                    }
                }
                len = sendFrame(sfd, PUT, &put_msg);
                printf("Transmitted (src= %d) (PUT: %s)\n", idNo, put_msg.mPut.file);
                for (int i = 0; i < put_msg.mPut.lineNo; i++) {
                    printf("[%d]: '%s'\n", i, put_msg.mPut.lines[i]);
                }
                len = rcvFrame(sfd, &frame);
                if (frame.kind == ERROR) {
                    printf("Received (src= 0) (%s)\n\n", frame.msg.mError.error);
                }
                else {
                    printf("Received (src= 0) OK\n\n");
                }
            }
            // if "get" send a GET package conating client no. and object name to server. Wait to recieve either
            // an ERROR package if the object is not stored in the server's object table or an OK package with the
            // content of the object.
            if (!strcmp(token[1], "get")) {
                MSG get_msg;
                memset( (char *) &get_msg, 0, sizeof(get_msg) );
                get_msg.mGet.client = idNo;
                strcpy(get_msg.mGet.file,token[2]);
                len = sendFrame(sfd, GET, &get_msg);
                printf("Transmitted (src= %d) (GET: %s)\n", idNo, get_msg.mGet.file);
                len = rcvFrame(sfd, &frame);
                if (frame.kind == ERROR) {
                    printf("Received (src= 0) (%s)\n", frame.msg.mError.error);
                    printf("[0]: object not found;\n\n");
                }
                else {
                    printf("Recieved (src= 0) OK\n");
                    for (int i = 0; i < frame.msg.mPut.lineNo; i++) {
                        printf("[%d]: '%s'\n", i, frame.msg.mPut.lines[i]);
                    }
                    printf("\n");
                }
            }
            // if "delete" send a DELETE package containing client no and object name to the server.
            // Wait to receive either an ERROR package if client is not the owner of the object or if
            // the object does not exist, or an OK package if object was successfully deleted from the server's table
            if (!strcmp(token[1], "delete")) {
                MSG msg;
                memset( (char *) &msg, 0, sizeof(msg) );
                msg.mDlt.client = idNo;
                strcpy(msg.mDlt.file,token[2]);
                len = sendFrame(sfd, DELETE, &msg);
                printf("Transmitted (src= %d) (DELETE: %s)\n", idNo, msg.mGet.file);
                len = rcvFrame(sfd, &frame);
                if (frame.kind == ERROR) {
                    if (frame.msg.mError.client != idNo) {
                        printf("Received (src= 0) (%s)\n\n", frame.msg.mError.error);
                    }
                    else {
                        printf("Received (src= 0) (%s)\n\n", frame.msg.mError.error);
                    }
                }
                else {
                    printf("Recieved (src= 0) OK\n\n");
                }
            }
            // if "quit" client breaks from loop and exits the program
            if (!strcmp(token[1], "quit")) {
                printf("quitting\n\n");
                break;
            }
            // if "gtime" client sends a GTIME package to the server and waits to recieve a TIME pacakage containing the
            // time in seconds since the server started operation.
            else if (!strcmp(token[1], "gtime")) {
                MSG msg;
                memset( (char *) &msg, 0, sizeof(msg) );
                msg.mGT.client = idNo;
                len = sendFrame(sfd, GTIME, &msg);
                printf("Transmitted (src= %d) GTIME\n", idNo);
                len = rcvFrame(sfd, &frame);
                printf("Received (src= 0) (TIME:    %ld)\n\n", frame.msg.mTime.time);
            }
            // if "delay" client sleeps for the amount of time mentioned in millliseconds after the delay message.
            if (!strcmp(token[1], "delay")) {
                printf("*** Entering a delay period of %s msec\n", token[2]);
                sleep(stoi(token[2])/1000); // *********find if better way for this ***********
                printf("*** Exiting delay period\n\n");
            }
        }
    }
    // close the file and socket and print the time elapsed for the client.
    fclose(fptr);
    printf("do_client: closing socket \n\n"); close(sfd);
    end = times(&tmsend);
    pr_times(end-start, &tmsstart, &tmsend);
}
// −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
// do_server: clients are numbered i= 1,2, ..., NCLIENT.
//            The done[] flags:
//            done[i]= −1 when client i has not connected
//                   =  0 when client i has connected but not finished
//                   = +1 when client i has finished or server lost connection
//
void do_server (int portNo)
{
    int idNo = 0;
    int i, N, len, rval, timeout, done[NCLIENT+2];
    int newsock[NCLIENT+1];
    char buf[MAXBUF], line[MAXLINE];
    MSG msg;
    MSG send_msg;
    FRAME frame;
    // create array of msg
    MSG_PUT put_messages[1000];
    MSG_GET get_messages[1000];
    int array_counter = 0;
    clock_t start, end;
    struct tms  tmsstart, tmsend;

    struct pollfd pfd[NCLIENT+2];
    struct sockaddr_in  from;
    socklen_t           fromlen;
 
    for (i= 0; i <= NCLIENT+1; i++) done[i]= -1;

    // prepare for noblocking I/O polling from the managing socket
    timeout= 0;
    pfd[0].fd = STDIN_FILENO; // keyboard input
    pfd[0].events = POLLIN;
    pfd[0].revents = 0;    
    pfd[1].fd=  serverListen (portNo, NCLIENT);;
    pfd[1].events= POLLIN;
    pfd[1].revents= 0;

    printf("a3w23: do_server\n");
    printf ("Server is accepting connections (port= %d)\n\n", portNo);
    printf("press l to view list and press q to quit server program\n\n");
    start = times(&tmsstart);

    N= 2;		// N descriptors to poll
    while(1) {
        rval= poll (pfd, N, timeout);
        if(rval < 0){
            printf("error\n\n");
        }
        // poll stdin
        if (pfd[0].revents & POLLIN) {
            char buffer[2];
            fgets(buffer, sizeof(buffer), stdin);
            //int read_result = read(pfd[0].fd, buffer, 4);
            if (!strcmp(buffer, "q")) {
                break;
            }
            if (!strcmp(buffer, "l")) {
                printf("\nStored object table:\n");
                for (int i = 0; i < array_counter; i++) {
                    printf("(owner= %d, name= %s)\n", put_messages[i].client, put_messages[i].file);
                    for (int j = 0; j < put_messages[i].lineNo; j++) {
                        printf("[%d]: '%s'\n", j, put_messages[i].lines[j]);
                    }
                }
            }
        }
        
        if ( (N < NCLIENT+2) && (pfd[1].revents & POLLIN) ) {
           // accept a new connection
            fromlen= sizeof(from);
            newsock[N]= accept(pfd[1].fd, (SA *) &from, &fromlen);

            pfd[N].fd= newsock[N];
            pfd[N].events= POLLIN;
            pfd[N].revents= 0;
            done[N]= 0;
            N++;
        }
        // check client sockets
        //
        for (i= 2; i <= N; i++) {
            if ((done[i] == 0) && (pfd[i].revents & POLLIN)) { 
                len= rcvFrame(pfd[i].fd, &frame);
                if (len == 0) {
                    printf ("server lost connection with client\n");
                    done[i]= 1;
                    continue;      // start a new for iteration
                }	   
                if (frame.kind == HELLO) {
                    printf("Received (src= %d) (HELLO, idNumber= %d)\n", frame.msg.mHello.client, frame.msg.mHello.client);
                    len = sendFrame(pfd[i].fd, OK, &msg);
                    printf("Transmitted (src= 0) OK\n\n");
                }
                if (frame.kind == PUT) {
                    printf("Recieved (src= %d) (PUT: %s)\n",frame.msg.mPut.client, frame.msg.mPut.file);
                    for (int i = 0; i < frame.msg.mPut.lineNo; i++) {
                        printf("[%d]: '%s'\n", i, frame.msg.mPut.lines[i]);
                    }
                    MSG msg;
                    memset( (char *) &msg, 0, sizeof(msg) );
                    MSG send_msg;
                    memset( (char *) &send_msg, 0, sizeof(send_msg) ); 
                    msg = frame.msg;
                    int file_found = 0;
                    // save client no. in table
                    for (int j = 0; j < array_counter; j++) {
                        // check if file name same
                        if (!strcmp(put_messages[j].file, msg.mPut.file)) {
                            file_found = 1;
                            send_msg.mError.client = msg.mPut.client;
                            strcpy(send_msg.mError.error, "ERROR: object already exists");
                            len = sendFrame(pfd[i].fd, ERROR, &send_msg);
                            printf("Transmitted (src= 0) (ERROR: object already exists)\n\n");
                        }
                    }   
                    if (file_found == 0) {
                        // save file in table
                        put_messages[array_counter].client = msg.mPut.client;
                        strcpy(put_messages[array_counter].file, msg.mPut.file);
                        put_messages[array_counter].lineNo = msg.mPut.lineNo;
                        for (int i = 0; i < msg.mPut.lineNo; i++) {
                            // save the lines
                            strcpy(put_messages[array_counter].lines[i], msg.mPut.lines[i]);
                        }
                        len = sendFrame(pfd[i].fd, OK, &send_msg);
                        printf("Transmitted (src= 0) OK\n\n");
                        array_counter++;
                    }
                }
                if (frame.kind == GTIME) {
                    MSG msg;
                    memset( (char *) &msg, 0, sizeof(msg) );
                    MSG send_msg;
                    memset( (char *) &send_msg, 0, sizeof(send_msg) );
                    msg = frame.msg;
                    printf("Received (src= %d) GTIME\n", msg.mGT.client);
                    end = times(&tmsend);
                    // make TIME package
                    clock_t  elapsed_seconds = end-start;
                    send_msg.mTime.client = msg.mGT.client;
                    send_msg.mTime.time = elapsed_seconds;
                    len = sendFrame(pfd[i].fd, TIME, &send_msg);
                    printf("Transmitted (src= 0) (TIME:    %ld)\n\n", send_msg.mTime.time);
                }
                if (frame.kind == GET) {
                    MSG msg;
                    memset( (char *) &msg, 0, sizeof(msg) );
                    MSG send_msg;
                    memset( (char *) &send_msg, 0, sizeof(send_msg) );
                    msg = frame.msg;
                    printf("Received (src= %d) (GET: %s)\n", msg.mGet.client, msg.mGet.file);
                    int found = 0;
                    for (int k = 0; k < array_counter; k++) {
                        // check if object name exists in table
                        if (!strcmp(put_messages[k].file, msg.mGet.file)) {
                            found = 1;
                            send_msg.mPut.client = msg.mGet.client;
                            strcpy(send_msg.mPut.file, msg.mGet.file);
                            send_msg.mPut.lineNo = put_messages[k].lineNo;
                            for (int j =0; j < put_messages[k].lineNo; j++) {
                                strcpy(send_msg.mPut.lines[j], put_messages[k].lines[j]);
                            }
                            len = sendFrame(pfd[i].fd, OK, &send_msg);
                            printf("Transmitted (src= 0) OK\n\n");
                        }
                    }
                    if (found == 0) {
                        send_msg.mError.client = msg.mGet.client;
                        strcpy(send_msg.mError.error, "ERROR: object not found");
                        len = sendFrame(pfd[i].fd, ERROR, &send_msg);
                        printf("Transmitted (src= 0) (ERROR: object not found)\n\n");
                    }
                }
                if (frame.kind == DELETE) {
                    MSG msg;
                    memset( (char *) &msg, 0, sizeof(msg) );
                    MSG send_msg;
                    memset( (char *) &send_msg, 0, sizeof(send_msg) );
                    msg = frame.msg;
                    int found = 0;
                    int deleted = 0;
                    printf("Received (src= %d) (DELETE: %s)\n", msg.mDlt.client, msg.mDlt.file);
                    int client_no = msg.mDlt.client;
                    for (int j = 0; j < array_counter; j++) {
                        // check if object exists in table
                        if (!strcmp(put_messages[j].file, msg.mDlt.file)) {
                            found = 1;
                            // check if sender of package is the owner of the object
                            if (put_messages[j].client == client_no) {
                                deleteElement(put_messages, put_messages[j].client, msg.mDlt.file);
                                array_counter--;
                                len = sendFrame(pfd[i].fd, OK, &msg);
                                printf("Transmitted (src= 0) OK\n\n");
                                deleted = 1;
                            }
                        }
                    }
                    // if sender not owner send ERROR package
                    if ((found == 1) && (deleted == 0)) {
                        send_msg.mError.client = msg.mGet.client;
                        strcpy(send_msg.mError.error, "ERROR: client not owner");
                        len = sendFrame(pfd[i].fd, ERROR, &send_msg);
                        printf("Transmitted (src= 0) (ERROR: client not owner)\n\n");
                    }
                    // else send different ERROR package
                    else if (deleted == 0) {
                        send_msg.mError.client = msg.mGet.client;
                        strcpy(send_msg.mError.error, "ERROR: object not found");
                        len = sendFrame(pfd[i].fd, ERROR, &send_msg);
                        printf("Transmitted (src= 0) (ERROR: object not found)\n\n");
                    }
                }
                // if DONE change done flag of the client to 1.
                if (frame.kind == DONE)
                    {printf ("%s: Done\n", line); done[i]= 1;}
            }	       
        }
    }	   

    // *************this is list command*************
    printf("\nStored object table:\n");
    for (int i = 0; i < array_counter; i++) {
        printf("(owner= %d, name= %s)\n", put_messages[i].client, put_messages[i].file);
        for (int j = 0; j < put_messages[i].lineNo; j++) {
            printf("[%d]: '%s'\n", j, put_messages[i].lines[j]);
        }
    }
    printf("\n");

    printf("quitting\n");
    printf ("do_server: server closing main socket (");
    for (i=1 ; i <= N-1; i++) printf ("done[%d]= %d, ", i, done[i]);
    printf (") \n\n");
    for (i=1; i <= N-1; i++) close(pfd[i].fd);
    close(pfd[1].fd);
    end = times(&tmsend);
    pr_times(end-start, &tmsstart, &tmsend);
}    
// ------------------------------
int main (int argc, char *argv[])
{
    if (argc < 2)  FATAL ("Usage: %s [-c|-s] serverName \n", argv[0]);

    if ( strstr(argv[1], "-c") != NULL) {
        if (argc != 6) FATAL ("Usage: %s -c idNumber inputFile serverAddress portNumber \n");
	    do_client(stoi(argv[2]), argv[3], argv[4], stoi(argv[5]));
    }	
    else if ( strstr(argv[1], "-s") != NULL)
    {
        if (argc != 3) FATAL ("Usage: %s -s portNumber \n");
        do_server(stoi(argv[2]));
    }

} 
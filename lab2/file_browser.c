/*
 * FILE: file_browser.c
 *
 * Description: A simple, iterative HTTP/1.0 Web server that uses the
 * GET method to serve static and dynamic content.
 *
 * Date: April 4, 2016
 */

#include <arpa/inet.h> // inet_ntoa
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#define LISTENQ  1024  // second argument to listen()
#define MAXLINE 1024   // max length of a line
#define RIO_BUFSIZE 1024

#define BACKLOG 9999   // the queue length of waiting connections

typedef struct {
    int rio_fd;                 // descriptor for this buf
    int rio_cnt;                // unread byte in this buf
    char *rio_bufptr;           // next unread byte in this buf
    char rio_buf[RIO_BUFSIZE];  // internal buffer
} rio_t;

// simplifies calls to bind(), connect(), and accept()
typedef struct sockaddr SA;

typedef struct {
    char filename[512];
    int browser_index;
    off_t offset;              // for support Range
    size_t end;
} http_request;

typedef struct {
    const char *extension;
    const char *mime_type;
} mime_map;

mime_map meme_types [] = {
    {".css", "text/css"},
    {".gif", "image/gif"},
    {".htm", "text/html"},
    {".html", "text/html"},
    {".jpeg", "image/jpeg"},
    {".jpg", "image/jpeg"},
    {".ico", "image/x-icon"},
    {".js", "application/javascript"},
    {".pdf", "application/pdf"},
    {".mp4", "video/mp4"},
    {".png", "image/png"},
    {".svg", "image/svg+xml"},
    {".xml", "text/xml"},
    {NULL, NULL},
};

char *default_mime_type = "text/plain";
char *Browser_Name[] = {"Chrome", "Safari", "Firefox", "IE"};
char *path;
char prefix[MAXLINE];

// set up an empty read buffer and associates an open file descriptor with that buffer
void rio_readinitb(rio_t *rp, int fd){
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

// utility function for writing user buffer into a file descriptor
ssize_t written(int fd, void *usrbuf, size_t n){
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;
    
    while (nleft > 0){
        if ((nwritten = write(fd, bufp, nleft)) <= 0){
            if (errno == EINTR)  // interrupted by sig handler return
                nwritten = 0;    // and call write() again
            else
                return -1;       // errorno set by write()
        }
        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}

void send_msg_to_client(int fd, int status, char *msg, const char* mem_type){
    char buf[RIO_BUFSIZE];
    // HTTP/1.1 200 Not Found
    // Content-length xxx
    // Message body
    sprintf(buf, "HTTP/1.1 %d %s\r\n", status, msg);
    if (mem_type != NULL){
        sprintf(buf + strlen(buf), "Content-type: %s\r\n\r\n", mem_type);
    }
    if (status == 404)
    {
        char *longmsg = "File not found!";
        sprintf(buf + strlen(buf), "Content-length: %lu\r\n\r\n", strlen(longmsg));
        sprintf(buf + strlen(buf), "%s", longmsg);
    }
    written(fd, buf, strlen(buf));
}

// Open Source function sendfile_to()
ssize_t sendfile_to(int out_fd, int in_fd, off_t *offset, size_t count)
{
    off_t orig;
    char buf[RIO_BUFSIZE];
    size_t toRead, numRead, numSent, totSent;
    
    if (offset != NULL) {
        
        /* Save current file offset and set offset to value in '*offset' */
        
        orig = lseek(in_fd, 0, SEEK_CUR);
        if (orig == -1)
            return -1;
        if (lseek(in_fd, *offset, SEEK_SET) == -1)
            return -1;
    }
    
    totSent = 0;
    
    while (count > 0) {
        if(RIO_BUFSIZE > count)
            toRead = count;
        else
            toRead = RIO_BUFSIZE;
        
        numRead = read(in_fd, buf, toRead);
        if (numRead == -1)
            return -1;
        if (numRead == 0)
            break;                      /* EOF */
        
        numSent = write(out_fd, buf, numRead);
        if (numSent == -1)
            return -1;
        if (numSent == 0)               /* Should never happen */
            perror("sendfile: write() transferred 0 bytes");
        
        count -= numSent;
        totSent += numSent;
    }
    
    if (offset != NULL) {
        /* Return updated file offset in '*offset', and reset the file offset
         to the value it had when we were called. */
        
        *offset = lseek(in_fd, 0, SEEK_CUR);
        if (*offset == -1)
            return -1;
        if (lseek(in_fd, orig, SEEK_SET) == -1)
            return -1;
    }
    return totSent;
}

/*
 *    This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n){
    int cnt;
    while (rp->rio_cnt <= 0){  // refill if buf is empty

        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
                           sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0){
            if (errno != EINTR) // interrupted by sig handler return
                return -1;
        }
        else if (rp->rio_cnt == 0)  // EOF
            return 0;
        else
            rp->rio_bufptr = rp->rio_buf; // reset buffer ptr
    }
    
    // copy min(n, rp->rio_cnt) bytes from internal buf to user buf
    cnt = n;
    if (rp->rio_cnt < n)
        cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}

// robustly read a text line (buffered)
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen){
    int n, rc;
    char c, *bufp = usrbuf;
    
    for (n = 1; n < maxlen; n++){
        if ((rc = rio_read(rp, &c, 1)) == 1){
            *bufp++ = c;
            if (c == '\n')
                break;
        } else if (rc == 0){
            if (n == 1)
                return 0; // EOF, no data read
            else
                break;    // EOF, some data was read
        } else
            return -1;    // error
    }
    *bufp = 0;
    return n;
}

// utility function to get the format size
void format_size(char* buf, struct stat *stat){
    if(S_ISDIR(stat->st_mode)){
        sprintf(buf, "%s", "[DIR]");
    } else {
        off_t size = stat->st_size;
        if(size < 1024){
            sprintf(buf, "%lld", size);
        } else if (size < 1024 * 1024){
            sprintf(buf, "%.1fK", (double)size / 1024);
        } else if (size < 1024 * 1024 * 1024){
            sprintf(buf, "%.1fM", (double)size / 1024 / 1024);
        } else {
            sprintf(buf, "%.1fG", (double)size / 1024 / 1024 / 1024);
        }
    }
}

// pre-process files in the "home" directory and send the list to the client
void handle_directory_request(int out_fd, int dir_fd, char *filename){
    // send response headers to client e.g., "HTTP/1.1 200 OK\r\n"
    send_msg_to_client(out_fd, 200, "OK", "text/html");
    
    // send responce body to client
    char msg[RIO_BUFSIZE];
    
    // get file directory
    DIR *d;
    struct dirent *dir;
    struct stat attrib;
    d = opendir(filename);
    
    memset(&msg, 0, sizeof(msg));
    strcat(msg, "<html><head><style>body{font-family: monospace; font-size: 13px;}td {padding: 1.5px 6px;}</style></head><body><table>");
    written(out_fd, msg, strlen(msg));
    
    // read directory
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if ((dir -> d_name)[0] == '.'){
                continue;
            }
            char dir_name[MAXLINE];

            stat(dir -> d_name, &attrib);
            char date[20];
            strftime(date, 20, "%y-%m-%d %H:%m", gmtime(&(attrib.st_ctime)));
            sprintf(dir_name, "<tr><td><a href=\"./%s\">%s</a></td><td>%s</td><td>%lld</td></tr>", dir->d_name, dir->d_name, date, attrib.st_size);
            written(out_fd, dir_name, strlen(dir_name));
        }
        closedir(d);
    }
    
    // send the file buffers to the client
    
    // send recent browser data to the client
    memset(&msg, 0, sizeof(msg));
    strcat(msg, "</table><table><tr><td>The last 10 visited browsers:</td>");
    FILE *fp;
    
    written(out_fd, msg, strlen(msg));
    memset(&msg, 0, sizeof(msg));
    
    char browser_file_name[MAXLINE];
    strcpy(browser_file_name, prefix);
    strcat(browser_file_name, "recent_browser.txt");
    //printf("this is browser_file_name: %s\n", browser_file_name);
    if((fp=fopen(browser_file_name,"r"))!=NULL){
    //if((fp=fopen("./recent_browser.txt","r"))==NULL){
        for (int i = 0; i < 10; i++)
        {
            if (feof(fp)){
                break;
            }
            char browser_string[MAXLINE];
            char browser[4];
            fgets(browser, sizeof(browser), fp);
            int browser_index = atoi(browser);
            sprintf(browser_string, "<td>%s</td>", Browser_Name[browser_index]);
            strcat(msg, browser_string);
        }
        fclose(fp);
    }
    
    

    
    written(out_fd, msg, strlen(msg));
    memset(&msg, 0, sizeof(msg));
    
    strcat(msg, "</tr>\n");
    strcat(msg, "<td>The corresponding IP address:</td>");
    
    written(out_fd, msg, strlen(msg));
    memset(&msg, 0, sizeof(msg));
    
    char ip_address[20];    //length of an ip address XXX.XXX.XXX.XXX\n

    char ip_file_name[MAXLINE];
    strcpy(ip_file_name, prefix);
    strcat(ip_file_name, "ip_address.txt");
    // read recent ip address from file
    //printf("this is ip_file_name: %s\n", ip_file_name);
    if((fp=fopen(ip_file_name,"r")) != NULL){
    //if((fp=fopen("./ip_address.txt","r"))==NULL){
        for (int i = 0; i < 10; i++)
        {
            if (feof(fp)){
                break;
            }
            char ip_string[MAXLINE];
            fgets(ip_address, sizeof(ip_address), fp);
            sprintf(ip_string, "<td>%s</td>", ip_address);
            strcat(msg, ip_string);
        }
        fclose(fp);
    }
    
    strcat(msg, "</table>");
    written(out_fd, msg, strlen(msg));
    //printf("quiting handle_directory_request\n");
}

// utility function to get the MIME (Multipurpose Internet Mail Extensions) type
static const char* get_mime_type(char *filename){
    char *dot = strrchr(filename, '.');
    if(dot){ // strrchar Locate last occurrence of character in string
        mime_map *map = meme_types;
        while(map->extension){
            if(strcmp(map->extension, dot) == 0){
                return map->mime_type;
            }
            map++;
        }
    }
    return default_mime_type;
}

// open a listening socket descriptor using the specified port number.
int open_listenfd(int port){
    struct sockaddr_in serveraddr;
    int Listenfd;
    
    // create a socket descriptor
    // 6 is TCP's protocol number
    // enable this, much faster : 4000 req/s -> 17000 req/s
    if ((Listenfd = socket(AF_INET, SOCK_STREAM, 6)) == -1) {
        perror("client socket creation error\n");
        exit(1);
    }
    
    /* Prepare the socket address structure of the server */
    serveraddr.sin_family = AF_INET;       // host byte order
    serveraddr.sin_port = htons(port);     // short, network byte order
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP address "localhost"
    memset(&(serveraddr.sin_zero), '\0', 8); // zero the rest of the struct
    
    int on=1;
    if((setsockopt(Listenfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)))<0)
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // eliminate "Address already in use" error from bind.
    if (bind(Listenfd, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr)) == -1) {
        perror("bind error\n");
        exit(1);
    }
    
    // Listenfd will be an endpoint for all requests to port
    // on any IP address for this host
    if (listen(Listenfd, BACKLOG) == -1) {
        perror("listen error\n");
        exit(1);
    }
    
    printf("Server is up and listening at port %d\n", port);
    
    // make it a listening socket ready to accept connection requests
    return Listenfd;
}

// decode url
void url_decode(char* src, char* dest, int max) {
    
}

// parse request to get url, extract for the IP address and browser type.
void parse_request(int fd, http_request *req, struct sockaddr_in *clientaddr){
    char usrbuf[MAXLINE];
    
    if (path != NULL){
        strcpy(prefix, path);
        if (prefix[strlen(prefix) - 1] != '/'){
            strcat(prefix, "/");
        }
    }
    else{
        strcpy(prefix, "./");
    }
    
    // Rio (Robust I/O) Buffered Input Functions
    rio_t *rp = (rio_t *)malloc(sizeof(rio_t));
    rio_readinitb(rp, fd);
    
    // update ip address data
    FILE *fp;
    char recent_address[MAXLINE];
    char ip_address[20];    //length of an ip address XXX.XXX.XXX.XXX\n
    
    // read recent ip address from file
    memset(&recent_address, 0, sizeof(recent_address));
    char ip_file_name[MAXLINE];
    strcpy(ip_file_name, prefix);
    strcat(ip_file_name, "ip_address.txt");
    if((fp=fopen(ip_file_name,"r"))!=NULL){
        // read the most recent 9 records
        for (int i = 0; i < 9; i++)
        {
            fgets(ip_address, sizeof(ip_address), fp);
            strcat(recent_address, ip_address);
            if (feof(fp)){
                break;
            }
        }
        fclose(fp);
    }

    
    // extract ip address from clientaddr
    memset(&ip_address, 0, sizeof(ip_address));
    sprintf(ip_address, "%s", inet_ntoa(clientaddr -> sin_addr));
    
    // write updated ip address into file
    if((fp=fopen(ip_file_name,"w"))==NULL){
        printf("Cannot open file ./ip_address.txt, exit!");
        exit(1);
    }
    char output[MAXLINE * 10];
    
    // add client address to the top of the list
    sprintf(output, "%s\n", ip_address);
    strcat(output, recent_address);
    fputs(output, fp);
    fclose(fp);
    
    // read all
    
    memset(&usrbuf, 0, strlen(usrbuf));
    rio_readlineb(rp, usrbuf, MAXLINE);
    char request_type[MAXLINE];
    char url[MAXLINE];
    char protocol[MAXLINE];
    sscanf(usrbuf, "%s %s %s", request_type, url, protocol);
    printf("request_type = %s\n", request_type);
    if (strstr(request_type, "GET")){
        char *dir = url;
        dir++;
        if (strlen(dir) == 0){
            strcpy(req -> filename, prefix);
        }
        else{
            char dir_name[MAXLINE];
            memset(&dir_name, 0, sizeof(dir_name));
            strcat(dir_name, prefix);
            strcat(dir_name, dir);
            strcpy(req -> filename, dir_name);
        }
        //printf("This is filename: %s\n", req -> filename);
    }
    else{
        printf("Request type is not GET\n");
        return;
    }
    
    memset(&usrbuf, 0, strlen(usrbuf));
    req -> browser_index = 0;
    while (strcmp(usrbuf, "\r\n") != 0){
        memset(&usrbuf, 0, strlen(usrbuf));
        rio_readlineb(rp, usrbuf, MAXLINE);
        
        //if (strcasecmp(msg, "User-Agent:") == 0){  // extract browser type
        if (strstr(usrbuf, "User-Agent:") != NULL){
            strtok(usrbuf, " ");
            char *type = strtok(NULL, "\n");
            printf("This is browser type: %s\n", type);
            if (strstr(type, "Chrome") != NULL){
                req -> browser_index = 0;   // 0 for Chrome
            }
            else if (strstr(type, "Firefox") != NULL){
                req -> browser_index = 2;   // 2 for Firefox
            }
            else if (strstr(type, "Safari") != NULL){
                req -> browser_index = 1; // 1 for Safari
            }
            else {
                req -> browser_index = 3; // 3 for IE
            }
            //printf("quiting parse_request()\n");

            break;
        }
    }
    
    // update recent browser data
    FILE *ffp;
    char recent_browser[MAXLINE];
    char browser[10];
    memset(&recent_browser, 0, sizeof(recent_browser));
    char browser_file_name[MAXLINE];
    strcpy(browser_file_name, prefix);
    strcat(browser_file_name, "recent_browser.txt");
    if((ffp=fopen(browser_file_name,"r")) != NULL){
        // read most recent 9 browser name from file
        for (int i = 0; i < 9; i++)
        {
            fgets(browser, sizeof(browser), ffp);
            strcat(recent_browser, browser);
            if (feof(ffp)){
                break;
            }
        }
        fclose(ffp);
    }
    
        // write updated browser name into file
    if((ffp=fopen(browser_file_name,"w"))==NULL){
        printf("Cannot open file ./recent_browser.txt, exit!");
        exit(1);
    }
    //printf("req -> browser_index = %d\n", req -> browser_index);
    sprintf(output,  "%d\n", req -> browser_index);
    strcat(output, recent_browser);
    fputs(output, ffp);
    fclose(ffp);
    
    // decode url
}

// log files
void log_access(int status, struct sockaddr_in *c_addr, http_request *req){
    
}

// echo client error e.g. 404
void client_error(int fd, int status, char *msg, char *longmsg){
    
}

// serve static content
void serve_static(int out_fd, int in_fd, http_request *req,
                  size_t total_size){
    
    // send response headers to client e.g., "HTTP/1.1 200 OK\r\n"
    send_msg_to_client(out_fd, 200, "OK", get_mime_type(req->filename));
    
    // send response body to client
    /*
    off_t offset = req->offset;
    while(offset < req->end){
        if (sendfile_to(out_fd, in_fd, &offset, req->end - req->offset) <= 0) {
            break;
        }
        break;
    }
     */
    char buf[MAXLINE];
    
    rio_t *rp = (rio_t *)malloc(sizeof(rio_t));
    rio_readinitb(rp, in_fd);
    int n;
    while ((n = rio_readlineb(rp, buf, MAXLINE)) > 0) {
        written(out_fd, buf, strlen(buf));
    }
    close(out_fd);
}

// handle one HTTP request/response transaction
void process(int fd, struct sockaddr_in *clientaddr){
    printf("accept request, fd is %d, pid is %d\n", fd, getpid());
    http_request req;
    parse_request(fd, &req, clientaddr);
    
    struct stat sbuf;
    int status = 200; //server status init as 200
    int ffd = open(req.filename, O_RDONLY, 0);
    if(ffd <= 0){
        // detect 404 error and print error log
        status = 404;
        send_msg_to_client(fd, status, "Not Found", NULL);
        
    } else {
        // get descriptor status
        fstat(ffd, &sbuf);
        if(S_ISREG(sbuf.st_mode)){
            if (req.end == 0){
                req.end = sbuf.st_size;
            }
            // server serves static content
            //printf("entering server_static()\n");
            serve_static(fd, ffd, &req, sbuf.st_size);
            
        }
        else if(S_ISDIR(sbuf.st_mode)){
            // server handle directory request
            //printf("entering handle_directory_request()\n");
            handle_directory_request(fd, ffd, req.filename);

        } else {
            // detect 400 error and print error log
            status = 400;
            send_msg_to_client(fd, status, "Bad Request", NULL);
        }
        close(ffd);
    }
    
    // print log/status on the terminal
    log_access(status, clientaddr, &req);
}

// main function:
// get the user input for the file directory and port number
int main(int argc, char** argv){
    struct sockaddr_in clientaddr;
    int default_port = 9998,
    listenfd,
    connfd;
    pid_t pid;
    
    // get the name of the current working directory
    path = NULL;
    
    // user input checking
    if (argc >= 3){
        default_port = atoi(argv[1]);
        path = argv[2];
        DIR *d;
        d = opendir(path);
        if (!d){
            printf("No such directory!\n");
            return 0;
        }
        closedir(d);
    }
    else if (argc >= 2){
        default_port = atoi(argv[1]);
    }
    
    listenfd = open_listenfd(default_port);
    
    // ignore SIGPIPE signal, so if browser cancels the request, it
    // won't kill the whole process.
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    
    while(1){
        // permit an incoming connection attempt on a socket.
        memset(&clientaddr, 0, sizeof(struct sockaddr));
        socklen_t sin_size = sizeof(struct sockaddr_in);
        if ((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &sin_size)) == -1) {
            perror("accept error");
            exit(1);
        }
        
        // fork children to handle parallel clients
        pid = fork();
        if (pid > 0){
            printf("i am the parent process, my process id is %d, my child's pid is %d\n",getpid(), pid);
            close(connfd);
            wait(&pid);
        }
        else if (pid == 0){
            printf("i am the child process, my process id is %d\n",getpid());
            close(listenfd);
            process(connfd, &clientaddr);
            close(connfd);
            exit(0);
        }
        else{
            printf("fork failed\n");
        }
    }
    
    return 0;
}
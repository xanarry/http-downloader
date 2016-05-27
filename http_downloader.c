#include <stdio.h>//printf
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>//ip
#include <fcntl.h>//open
#include <unistd.h>//write
#include <netdb.h>//DNS
#include <stdlib.h>//exit
#include <sys/stat.h>//stat get file size
#include <sys/time.h>//time diff

struct HTTP_RES_HEADER//record some info of http response header
{
    int status_code;//HTTP/1.1 '200' OK
    char content_type[128];//Content-Type: application/gzip
    long content_length;//Content-Length: 11683079
};

void parse_url(const char *url, char *host, int *port, char *file_name)
{
	/*parsing url, and get host, port and file name*/
    int j = 0;
    int start = 0;
    *port = 80;
    char *patterns[] = {"http://", "https://", NULL};

    for (int i = 0; patterns[i]; i++)//remove http and https
        if (strncmp(url, patterns[i], strlen(patterns[i])) == 0)
            start = strlen(patterns[i]);

    for (int i = start; url[i] != '/' && url[i] != '\0'; i++, j++)
        host[j] = url[i];
    host[j] = '\0';

	//get port, defalt is 80
    char *pos = strstr(host, ":");
    if (pos)
        sscanf(pos, ":%d", port);

	for (int i = 0; i < (int)strlen(host); i++)
	{
		if (host[i] == ':')
		{
			host[i] = '\0';
			break;
		}
	}

	//get file name
    j = 0;
    for (int i = start; url[i] != '\0'; i++)
    {
        if (url[i] == '/')
        {
            if (i !=  strlen(url) - 1)
                j = 0;
            continue;
        }
        else
            file_name[j++] = url[i];
    }
    file_name[j] = '\0';
}

struct HTTP_RES_HEADER parse_header(const char *response)
{
	/*get info from response header*/
    struct HTTP_RES_HEADER resp;

    char *pos = strstr(response, "HTTP/");
    if (pos)//get status code
        sscanf(pos, "%*s %d", &resp.status_code);

    pos = strstr(response, "Content-Type:");
    if (pos)//get content type
        sscanf(pos, "%*s %s", resp.content_type);

    pos = strstr(response, "Content-Length:");
    if (pos)//content length
        sscanf(pos, "%*s %ld", &resp.content_length);

    return resp;
}

void get_ip_addr(char *host_name, char *ip_addr)
{
	//get ip address by url
    struct hostent *host = gethostbyname(host_name);//此函数将会访问DNS服务器
    if (!host)
    {
        ip_addr = NULL;
        return;
    }

    for (int i = 0; host->h_addr_list[i]; i++)
    {
        strcpy(ip_addr, inet_ntoa( * (struct in_addr*) host->h_addr_list[i]));
        break;
    }
}


void progress_bar(long cur_size, long total_size, double speed)
{
	/*show progress bar*/
    float percent = (float) cur_size / total_size;
    const int numTotal = 50;
    int numShow = (int)(numTotal * percent);

    if (numShow == 0)
        numShow = 1;

    if (numShow > numTotal)
        numShow = numTotal;

    char sign[51] = {0};
    memset(sign, '=', numTotal);

    printf("\r%.2f%%[%-*.*s] %.2f/%.2fMB %4.0fkb/s", percent * 100, numTotal, numShow, sign, cur_size / 1024.0 / 1024.0, total_size / 1024.0 / 1024.0, speed);
    fflush(stdout);

    if (numShow == numTotal)
        printf("\n");
}

unsigned long get_file_size(const char *filename)
{
    struct stat buf;
    if (stat(filename, &buf) < 0)
        return 0;
    return (unsigned long) buf.st_size;
}

void download(int client_socket, char *file_name, long content_length)
{
    long hasrecieve = 0;
    struct timeval t_start, t_end;
    int mem_size = 8192;//buffer size 8K
    int buf_len = mem_size;
    int len;

	//create a file
    int fd = open(file_name, O_CREAT | O_WRONLY, S_IRWXG | S_IRWXO | S_IRWXU);
    if (fd < 0)
    {
        printf("fail to create file!\n");
        exit(0);
    }

    char *buf = (char *) malloc(mem_size * sizeof(char));

	//从套接字流中读取文件流
    long diff = 0;
    int prelen = 0;
    double speed;

    while (hasrecieve < content_length)
    {
        gettimeofday(&t_start, NULL ); //获取开始时间
        len = read(client_socket, buf, buf_len);
        write(fd, buf, len);
        gettimeofday(&t_end, NULL ); //获取结束时间

        hasrecieve += len;//更新已经下载的长度

        //计算速度
        if (t_end.tv_usec - t_start.tv_usec >= 0 &&  t_end.tv_sec - t_start.tv_sec >= 0)
            diff += 1000000 * ( t_end.tv_sec - t_start.tv_sec ) + (t_end.tv_usec - t_start.tv_usec);//us

        if (diff >= 1000000)//当一个时间段大于1s=1000000us时, 计算一次速度
        {
            speed = (double)(hasrecieve - prelen) / (double)diff * (1000000.0 / 1024.0);
            prelen = hasrecieve;//清零下载量
            diff = 0;//清零时间段长度
        }

        progress_bar(hasrecieve, content_length, speed);

        if (hasrecieve == content_length)
            break;
    }
}

int main(int argc, char const *argv[])
{
    /* 命令行参数: 接收两个参数, 第一个是下载地址, 第二个是文件的保存位置和名字, 下载地址是必须的, 默认下载到当前目录
     * 示例: ./download http://www.baidu.com baidu.html
     */
    char url[2048] = "127.0.0.1";//设置默认地址为本机,
    char host[64] = {0};//远程主机地址
    char ip_addr[16] = {0};//远程主机IP地址
    int port = 80;//远程主机端口, http默认80端口
    char file_name[256] = {0};//下载文件名

    if (argc == 1)
    {
        printf("url address is nessessary\n");
        exit(0);
    }
    else
        strcpy(url, argv[1]);

    parse_url(url, host, &port, file_name);

    if (argc == 3)
    {
        printf("\tyou rename file as: %s\n", argv[2]);
        strcpy(file_name, argv[2]);
    }

    get_ip_addr(host, ip_addr);//调用函数同访问DNS服务器获取远程主机的IP
    if (strlen(ip_addr) == 0)
    {
        printf("fail to get ip address\n");
        return 0;
    }

    printf("\t     URL : %s\n", url);
    printf("\t    host : %s\n", host);
    printf("\t      IP : %s\n", ip_addr);
    printf("\t    PORT : %d\n", port);
    printf("\tFileName : %s\n\n", file_name);

	//set http request header
    char header[2048] = {0};
    sprintf(header, \
            "GET %s HTTP/1.1\r\n"\
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"\
            "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537(KHTML, like Gecko) Chrome/47.0.2526Safari/537.36\r\n"\
            "Host: %s\r\n"\
            "Connection: keep-alive\r\n"\
            "\r\n"\
        ,url, host);

    int client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket < 0)
    {
        printf("套接字创建失败: %d\n", client_socket);
        exit(-1);
    }

	//创建IP地址结构体
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip_addr);
    addr.sin_port = htons(port);

	//connect host
    int res = connect(client_socket, (struct sockaddr *) &addr, sizeof(addr));
    if (res == -1)
    {
        printf("fail to connect, error: %d\n", res);
        exit(-1);
    }

    //send request
    write(client_socket, header, strlen(header));//write系统调用, 将请求header发送给服务器

    int mem_size = 4096;
    int length = 0;
    int len;
    char *buf = (char *) malloc(mem_size * sizeof(char));
    char *response = (char *) malloc(mem_size * sizeof(char));

    while ((len = read(client_socket, buf, 1)) != 0)
    {
        if (length + len > mem_size)
        {
        	//dinamic apply for memory, looks here is not nessessary
            mem_size *= 2;
            char * temp = (char *) realloc(response, sizeof(char) * mem_size);
            if (temp == NULL)
            {
                printf("segment failed\n");
                exit(-1);
            }
            response = temp;
        }

        buf[len] = '\0';
        strcat(response, buf);

		//find seperator of header and content \n\r\n\r
        int flag = 0;
        for (int i = strlen(response) - 1; response[i] == '\n' || response[i] == '\r'; i--, flag++);
        if (flag == 4)
            break;

        length += len;
    }

    struct HTTP_RES_HEADER resp = parse_header(response);

    if (resp.status_code != 200)
    {
        printf("Error: %d\n", resp.status_code);
        return 0;
    }
    download(client_socket, file_name, resp.content_length);

    if (resp.content_length == get_file_size(file_name))
        printf("\nFile: %s has finished download! ^_^\n\n", file_name);
    else
    {
        remove(file_name);
        printf("\nFile broken!\n\n");
    }
    shutdown(client_socket, 2);//关闭套接字的接收和发送
    return 0;
}
